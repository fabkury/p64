#include "policy.hpp"

namespace p64::makapix::policy {

int64_t next_refresh_after_load(uint32_t age_s, uint32_t interval_s, int64_t now_us) {
  return age_s >= interval_s ? 0 : now_us + static_cast<int64_t>(interval_s - age_s) * kSecond;
}

StepStart begin_step(bool refreshing, int64_t walk_last_us, int64_t now_us) {
  StepStart s{};
  s.stale = refreshing && now_us - walk_last_us > kWalkIdleUs;
  s.first = !refreshing || s.stale;
  return s;
}

size_t drop_oversized(content::MakapixEntries &page, uint16_t max_side) {
  const size_t before = page.size();
  page.erase(std::remove_if(page.begin(), page.end(),
                            [max_side](const content::MakapixEntry &e) { return !content::fits_side(e, max_side); }),
             page.end());
  return before - page.size();
}

PageOutcome after_page(size_t walked, size_t cap, bool more, bool listed_none, bool index_empty) {
  PageOutcome o{};
  o.done = !more || walked >= cap || listed_none;
  o.install_now = !o.done && index_empty;
  return o;
}

int64_t retry_delay_us(uint32_t fail_streak, const std::string &error) {
  if (error == "needs pairing") return kRetryMaxUs;
  int64_t wait = kRetryBaseUs;
  for (uint32_t i = 1; i < fail_streak && wait < kRetryMaxUs; ++i) wait *= 2;
  return std::min(wait, kRetryMaxUs);
}

JobDisposition job_disposition(bool online, bool is_like, bool is_followed, bool someone_waits) {
  if (online || is_like) return JobDisposition::Run;
  if (is_followed && !someone_waits) return JobDisposition::Park;
  return JobDisposition::Fail;
}

SweepVerdict sweep_verdict(int64_t mtime, int64_t now, uint32_t older_than_s, int64_t floor) {
  if (mtime > now + 86400) return SweepVerdict::Suspect;
  if (mtime < floor) return SweepVerdict::Delete;  // written under an untrusted clock
  if (mtime >= now) return SweepVerdict::Keep;     // up to a day ahead: recent
  return now - mtime > older_than_s ? SweepVerdict::Delete : SweepVerdict::Keep;
}

}  // namespace p64::makapix::policy
