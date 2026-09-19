#include "mqtt.hpp"

#include <cstring>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "internal.hpp"
#include "mqtt_client.h"
#include "p64/system/event_bus.hpp"
#include "p64/system/settings.hpp"
#include "sdkconfig.h"

namespace p64::makapix::mqtt {
namespace {

using internal::TAG;

constexpr uint32_t kMaxAuthFailures = 8;  // consecutive refusals before the pairing counts as invalid

esp_mqtt_client_handle_t g_client = nullptr;
bool g_started = false;
bool g_connected = false;
uint32_t g_auth_failures = 0;
std::string g_uri, g_key, g_status_topic, g_lwt;
std::string g_ca, g_cert, g_priv;  // the client keeps pointers, so these must outlive it
esp_timer_handle_t g_heartbeat = nullptr;
std::string g_incoming;  // reassembly of a fragmented command
int g_incoming_total = 0;

std::string topic(const char *suffix) { return "makapix/player/" + g_key + "/" + suffix; }

void publish(const std::string &t, const std::string &payload, bool retain) {
  if (!g_client || !g_connected) return;
  esp_mqtt_client_publish(g_client, t.c_str(), payload.c_str(), static_cast<int>(payload.size()), 1, retain ? 1 : 0);
}

void publish_capabilities() {
  cJSON *root = cJSON_CreateObject();
  const esp_app_desc_t *app = esp_app_get_description();
  cJSON_AddStringToObject(root, "firmware_version", app ? app->version : "0");
  cJSON *features = cJSON_AddObjectToObject(root, "features");
  cJSON_AddItemToObject(features, "pause", cJSON_CreateObject());
  cJSON *brightness = cJSON_AddObjectToObject(features, "brightness");
  cJSON_AddNumberToObject(brightness, "min", 1);
  cJSON_AddNumberToObject(brightness, "max", 255);
  cJSON_AddNumberToObject(brightness, "step", 1);
  cJSON *rotation = cJSON_AddObjectToObject(features, "rotation");
  cJSON *values = cJSON_AddArrayToObject(rotation, "values");
  for (int v : {0, 90, 180, 270}) cJSON_AddItemToArray(values, cJSON_CreateNumber(v));
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!text) return;
  publish(topic("capabilities"), text, true);
  cJSON_free(text);
}

void on_event(void *, esp_event_base_t, int32_t event_id, void *event_data) {
  auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
  switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
    case MQTT_EVENT_CONNECTED: {
      g_connected = true;
      g_auth_failures = 0;
      ESP_LOGI(TAG, "MQTT connected to %s", g_uri.c_str());
      esp_mqtt_client_subscribe(g_client, topic("command").c_str(), 1);
      int32_t current = -1;
      if (internal::g_hooks.current_post_id) current = internal::g_hooks.current_post_id();
      publish_status(current);
      publish_capabilities();
      publish_state();
      {
        std::lock_guard<std::mutex> lock(internal::g_mutex);
        internal::g_status.mqtt_connected = true;
      }
      system::publish(system::Event::MakapixStateChanged);
      break;
    }
    case MQTT_EVENT_DISCONNECTED: {
      if (g_connected) ESP_LOGW(TAG, "MQTT disconnected");
      g_connected = false;
      {
        std::lock_guard<std::mutex> lock(internal::g_mutex);
        internal::g_status.mqtt_connected = false;
      }
      system::publish(system::Event::MakapixStateChanged);
      break;
    }
    case MQTT_EVENT_ERROR: {
      const esp_mqtt_error_codes_t *err = event->error_handle;
      if (err && err->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        ++g_auth_failures;
        ESP_LOGW(TAG, "MQTT connection refused (code %d), %lu in a row", static_cast<int>(err->connect_return_code),
                 static_cast<unsigned long>(g_auth_failures));
        if (g_auth_failures >= kMaxAuthFailures) {
          ESP_LOGE(TAG, "the broker keeps refusing the credentials: pairing is invalid, re-pair from the web UI");
          internal::set_error("the server refuses the credentials; pair again");
          internal::set_state(State::Invalid);
          esp_mqtt_client_stop(g_client);
        }
      } else if (err && err->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        ESP_LOGW(TAG, "MQTT transport error (esp-tls 0x%x, errno %d)", err->esp_tls_last_esp_err, err->esp_transport_sock_errno);
      }
      break;
    }
    case MQTT_EVENT_DATA: {
      // Commands are small, but esp-mqtt still fragments anything above its buffer.
      const bool command = event->topic_len > 0 && std::strncmp(event->topic, topic("command").c_str(), event->topic_len) == 0;
      if (event->current_data_offset == 0) {
        g_incoming.clear();
        g_incoming_total = event->total_data_len;
        if (!command) g_incoming_total = -1;  // not ours
      }
      if (g_incoming_total < 0) break;
      if (g_incoming_total > 32 * 1024) {
        g_incoming_total = -1;
        break;
      }
      g_incoming.append(event->data, static_cast<size_t>(event->data_len));
      if (static_cast<int>(g_incoming.size()) >= g_incoming_total) {
        internal::handle_command(g_incoming.data(), g_incoming.size());
        g_incoming.clear();
        g_incoming_total = -1;
      }
      break;
    }
    default: break;
  }
}

void heartbeat(void *) {
  if (!g_connected) return;
  int32_t current = -1;
  if (internal::g_hooks.current_post_id) current = internal::g_hooks.current_post_id();
  publish_status(current);
}

}  // namespace

bool start() {
  creds::Credentials c;
  {
    std::lock_guard<std::mutex> lock(internal::g_mutex);
    c = internal::g_creds;
  }
  if (!c.complete()) return false;
  if (g_client) {
    if (!g_started) {
      g_started = esp_mqtt_client_start(g_client) == ESP_OK;  // esp-mqtt reconnects on its own
    }
    return g_started;
  }
  g_key = c.player_key;
  g_ca = c.ca_pem;
  g_cert = c.cert_pem;
  g_priv = c.key_pem;
  const std::string host = c.mqtt_host.empty() ? CONFIG_P64_MAKAPIX_HOST : c.mqtt_host;
  const uint16_t port = c.mqtt_port ? c.mqtt_port : CONFIG_P64_MAKAPIX_MQTT_PORT;
  g_uri = "mqtts://" + host + ":" + std::to_string(port);
  g_status_topic = topic("status");
  g_lwt = "{\"player_key\":\"" + g_key + "\",\"status\":\"offline\"}";

  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.uri = g_uri.c_str();
  cfg.broker.verification.certificate = g_ca.c_str();
  cfg.credentials.client_id = g_key.c_str();
  cfg.credentials.username = g_key.c_str();
  cfg.credentials.authentication.password = "";
  cfg.credentials.authentication.certificate = g_cert.c_str();
  cfg.credentials.authentication.key = g_priv.c_str();
  cfg.session.last_will.topic = g_status_topic.c_str();
  cfg.session.last_will.msg = g_lwt.c_str();
  cfg.session.last_will.qos = 1;
  cfg.session.last_will.retain = false;
  cfg.session.keepalive = 60;
  cfg.session.disable_clean_session = false;
  cfg.network.reconnect_timeout_ms = 15000;
  cfg.network.timeout_ms = 10000;
  // The handshake peaks at about 2.7 KB of this stack (high-water mark measured on the
  // device); commands are small, and larger payloads arrive fragmented.
  cfg.task.stack_size = 6144;
  cfg.task.priority = 5;
  cfg.buffer.size = 4096;
  cfg.buffer.out_size = 2048;
  g_client = esp_mqtt_client_init(&cfg);
  if (!g_client) {
    ESP_LOGE(TAG, "MQTT client init failed");
    return false;
  }
  esp_mqtt_client_register_event(g_client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), on_event, nullptr);
  if (!g_heartbeat) {
    const esp_timer_create_args_t args = {heartbeat, nullptr, ESP_TIMER_TASK, "mqtt_hb", true};
    esp_timer_create(&args, &g_heartbeat);
    esp_timer_start_periodic(g_heartbeat, 30ULL * 1000000);
  }
  const esp_err_t err = esp_mqtt_client_start(g_client);
  if (err != ESP_OK) ESP_LOGE(TAG, "MQTT start: %s", esp_err_to_name(err));
  g_started = err == ESP_OK;
  return g_started;
}

void stop() {
  if (!g_client) return;
  if (g_connected) publish(g_status_topic, g_lwt, false);
  esp_mqtt_client_stop(g_client);
  esp_mqtt_client_destroy(g_client);
  g_client = nullptr;
  g_started = false;
  g_connected = false;
  {
    std::lock_guard<std::mutex> lock(internal::g_mutex);
    internal::g_status.mqtt_connected = false;
  }
}

bool connected() { return g_connected; }

void publish_status(int32_t current_post_id) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "player_key", g_key.c_str());
  cJSON_AddStringToObject(root, "status", "online");
  if (current_post_id >= 0) cJSON_AddNumberToObject(root, "current_post_id", current_post_id);
  const esp_app_desc_t *app = esp_app_get_description();
  cJSON_AddStringToObject(root, "firmware_version", app ? app->version : "0");
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!text) return;
  publish(g_status_topic, text, false);
  cJSON_free(text);
}

void publish_state() {
  const system::Settings s = system::settings();
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "is_paused", internal::g_hooks.is_paused ? internal::g_hooks.is_paused() : false);
  cJSON_AddNumberToObject(root, "brightness", s.brightness);
  cJSON_AddNumberToObject(root, "rotation", s.rotation);
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!text) return;
  publish(topic("state"), text, true);
  cJSON_free(text);
}

bool publish_view(const api::ViewEvent &v) {
  if (!g_connected) return false;
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "post_id", v.post_id);
  cJSON_AddStringToObject(root, "timestamp", v.timestamp.c_str());
  cJSON_AddStringToObject(root, "timezone", "");
  cJSON_AddStringToObject(root, "intent", v.intentional ? "artwork" : "channel");
  cJSON_AddNumberToObject(root, "play_order", v.play_order);
  cJSON_AddStringToObject(root, "channel", v.channel.c_str());
  cJSON_AddStringToObject(root, "player_key", g_key.c_str());
  if (!v.user_sqid.empty()) cJSON_AddStringToObject(root, "channel_user_sqid", v.user_sqid.c_str());
  if (!v.hashtag.empty()) cJSON_AddStringToObject(root, "channel_hashtag", v.hashtag.c_str());
  cJSON_AddBoolToObject(root, "request_ack", false);
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!text) return false;
  publish(topic("view"), text, false);
  cJSON_free(text);
  return true;
}

void publish_ack(const std::string &command_id, const char *status, const char *error) {
  if (command_id.empty()) return;
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "command_id", command_id.c_str());
  cJSON_AddStringToObject(root, "status", status);
  if (error) {
    cJSON_AddStringToObject(root, "error", error);
  } else {
    cJSON_AddNullToObject(root, "error");
  }
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!text) return;
  publish(topic("command/ack"), text, false);
  cJSON_free(text);
}

}  // namespace p64::makapix::mqtt
