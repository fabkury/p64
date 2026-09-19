// p64 -- Artwork: the bytes of one file, the decoder for its format and the scaler that
// fits its canvas into the panel. A FrameSource that produces panel-sized frames one at
// a time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "p64/decode/decoder.hpp"
#include "p64/gfx/frame.hpp"
#include "p64/gfx/scaler.hpp"
#include "p64/playback/frame_source.hpp"

namespace p64::playback {

// Limits from the spec (section 4.2).
constexpr int kMaxCanvasSide = 256;
constexpr size_t kMaxFileBytes = 5 * 1024 * 1024;

class Artwork : public FrameSource {
 public:
  // Takes the file bytes (they stay owned here; large buffers land in PSRAM), sniffs the
  // format and opens the decoder. `name` is for logs and status. On failure `error`
  // says why and the artwork is unusable.
  bool open(std::vector<uint8_t> bytes, std::string name, gfx::Rgb background, std::string &error);

  const std::string &name() const override { return name_; }
  const decode::Info &info() const { return decoder_->info(); }
  decode::Format format() const { return format_; }
  size_t file_bytes() const { return bytes_.size(); }
  bool is_open() const { return decoder_ && decoder_->is_open(); }

  // Decodes the next frame and scales it into `out` (bars in the background colour).
  // `delay_ms` receives the frame's delay after the browser rule. False on a decode
  // error; error() then says why.
  bool next_frame(gfx::Frame &out, uint32_t &delay_ms, int64_t due_us = 0) override;
  // True once the artwork is known to have a single frame (after its first frame).
  bool is_static() const override { return decoder_ && !decoder_->info().animated; }
  bool at_end() const { return decoder_ && decoder_->at_end(); }
  uint32_t frames_decoded() const { return frames_; }
  const gfx::Scaler &scaler() const { return scaler_; }
  const char *error() const override { return error_; }
  // Background for transparency and bars; takes effect on the frames decoded next.
  void set_background(gfx::Rgb background);

 private:
  std::vector<uint8_t> bytes_;
  std::string name_;
  std::unique_ptr<decode::Decoder> decoder_;
  gfx::Scaler scaler_;
  gfx::Rgb background_{};
  decode::Format format_ = decode::Format::Unknown;
  uint32_t frames_ = 0;
  const char *error_ = "";
};

}  // namespace p64::playback
