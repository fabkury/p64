#include "p64/playback/artwork.hpp"

#include <utility>

namespace p64::playback {

bool Artwork::open(std::vector<uint8_t> bytes, std::string name, gfx::Rgb background, std::string &error) {
  decoder_.reset();
  frames_ = 0;
  error_ = "";
  bytes_ = std::move(bytes);
  name_ = std::move(name);
  background_ = background;
  if (bytes_.size() > kMaxFileBytes) {
    error = "file larger than " + std::to_string(kMaxFileBytes) + " bytes";
    return false;
  }
  format_ = decode::sniff(bytes_.data(), bytes_.size());
  decoder_ = decode::create(format_);
  if (!decoder_) {
    error = format_ == decode::Format::Unknown ? "unknown file format"
                                               : std::string(decode::format_name(format_)) + " is not supported yet";
    return false;
  }
  if (!decoder_->open(bytes_.data(), bytes_.size(), background_)) {
    error = std::string(decode::format_name(format_)) + ": " + decoder_->error();
    decoder_.reset();
    return false;
  }
  const decode::Info &info = decoder_->info();
  if (info.width > kMaxCanvasSide || info.height > kMaxCanvasSide) {
    error = "canvas " + std::to_string(info.width) + "x" + std::to_string(info.height) + " exceeds " +
            std::to_string(kMaxCanvasSide) + "x" + std::to_string(kMaxCanvasSide);
    decoder_.reset();
    return false;
  }
  scaler_.configure(info.width, info.height, gfx::Frame::width(), gfx::Frame::height());
  return true;
}

bool Artwork::next_frame(gfx::Frame &out, uint32_t &delay_ms) {
  if (!decoder_ || !decoder_->is_open()) {
    error_ = "not open";
    return false;
  }
  if (!decoder_->next(delay_ms)) {
    error_ = decoder_->error();
    return false;
  }
  scaler_.scale(decoder_->canvas(), out, background_);
  ++frames_;
  return true;
}

void Artwork::set_background(gfx::Rgb background) {
  background_ = background;
  if (decoder_) decoder_->set_background(background);
}

}  // namespace p64::playback
