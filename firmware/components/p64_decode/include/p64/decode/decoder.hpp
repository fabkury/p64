// p64 -- Decoder: the one interface every artwork format implements. Decodes frame by
// frame from bytes in memory into an RGB888 canvas of the artwork's own size, with
// transparency already blended over the background colour in gamma space (spec 3.3),
// and reports each frame's delay after the browser rule (spec 4.3).
//
// No ESP-IDF includes in this component: it builds on the host for the tests and the
// decode benchmark.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "p64/gfx/frame.hpp"

namespace p64::decode {

enum class Format : uint8_t { Unknown = 0, Gif, Png, Apng, WebP, Bmp };

const char *format_name(Format f);

// Identifies the format from the file's first bytes (never from a file name). APNG is
// told from PNG by the presence of an acTL chunk before the first IDAT.
Format sniff(const uint8_t *data, size_t size);

// The browser rule for frame delays: GIF delays of 10 ms or less are shown for 100 ms;
// APNG and WebP delays of 0 are shown for 100 ms; everything else as stored.
uint32_t browser_delay_ms(Format format, uint32_t stored_ms);

struct Info {
  Format format = Format::Unknown;
  int width = 0;
  int height = 0;
  bool animated = false;      // more than one frame
  bool has_alpha = false;     // any transparency in the file
  uint32_t frame_count = 0;   // 0 when the format only reveals it by decoding to the end
};

class Decoder {
 public:
  virtual ~Decoder() = default;

  // `data` must stay valid until the decoder is destroyed or reopened. `background` is
  // what transparent pixels become. Returns false when the file cannot be parsed
  // (error() says why).
  virtual bool open(const uint8_t *data, size_t size, gfx::Rgb background) = 0;
  virtual const Info &info() const = 0;

  // Decodes the next frame into canvas(); after the last frame the next call starts the
  // loop again. `delay_ms` receives the frame's delay after the browser rule. Returns
  // false on a decode error (the decoder is closed then).
  virtual bool next(uint32_t &delay_ms) = 0;
  // width*height*3 bytes, valid after a successful next().
  virtual const uint8_t *canvas() const = 0;
  // True when the frame last decoded is the last of the loop.
  virtual bool at_end() const = 0;
  // Restarts from the first frame (the canvas is cleared to the background).
  virtual void rewind() = 0;
  // Changes the background: takes effect as pixels are composited from then on.
  virtual void set_background(gfx::Rgb background) = 0;

  virtual bool is_open() const = 0;
  virtual const char *error() const = 0;
};

// Creates a decoder for the format, or nullptr when the format is unknown or not built.
std::unique_ptr<Decoder> create(Format format);

}  // namespace p64::decode
