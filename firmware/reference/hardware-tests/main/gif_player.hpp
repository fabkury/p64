// p64 -- GIF playback core: decode (AnimatedGIF), composite, scale.
//
// Plain C++ with no ESP-IDF dependencies, so the same code is built on the PC by
// tools/gifcheck to compare every frame of every GIF against Pillow.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "AnimatedGIF.h"

namespace p64 {

// Decodes an animated GIF from memory and composites it into an RGB888 canvas of the
// GIF's logical screen size. Transparent pixels and "restore to background" show black;
// all four disposal methods are honoured (method 3 keeps a backup canvas).
class GifPlayer {
 public:
  GifPlayer();
  ~GifPlayer();
  GifPlayer(const GifPlayer &) = delete;
  GifPlayer &operator=(const GifPlayer &) = delete;

  // `data` must stay valid until close(). Returns false if the file cannot be parsed.
  bool open(const uint8_t *data, size_t size);
  void close();
  bool is_open() const { return open_; }
  int width() const { return w_; }
  int height() const { return h_; }

  // Decodes the next frame into the canvas, restarting from the first frame after the
  // last one. Returns false on a decode error (the player is closed then).
  bool next_frame();

  const uint8_t *canvas() const { return canvas_.get(); }  // width*height*3
  uint32_t frames_decoded() const { return frames_; }      // since open()
  uint32_t loops() const { return loops_; }                // wrap-arounds since open()
  int last_error() const { return last_error_; }           // AnimatedGIF error code
  // Delay stored in the GIF for the frame last decoded, in ms (0 when absent).
  int last_delay_ms() const { return last_delay_ms_; }
  // True when the frame last decoded is the animation's last one.
  bool at_end() const { return at_end_; }

 private:
  struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    uint8_t disposal = 0;
  };

  static void draw_callback(GIFDRAW *d);
  void on_line(const GIFDRAW *d);
  void begin_frame(const GIFDRAW *d);
  void dispose_previous();
  void rewind();
  void fail();

  std::unique_ptr<AnimatedGIF> gif_;  // ~27 KB, so it lives on the heap
  std::unique_ptr<uint8_t[]> canvas_;
  std::unique_ptr<uint8_t[]> backup_;
  int w_ = 0, h_ = 0;
  bool open_ = false;
  bool frame_started_ = false;
  bool at_end_ = false;
  Rect current_, previous_;
  uint32_t frames_ = 0, loops_ = 0;
  int last_error_ = 0;
  int last_delay_ms_ = 0;
};

// Fits an RGB888 image of one size into an RGB888 destination of another size, keeping
// the aspect ratio: nearest neighbour when enlarging, box average when shrinking, black
// bars around the image.
class Scaler {
 public:
  void configure(int src_w, int src_h, int dst_w, int dst_h);
  void scale(const uint8_t *src, uint8_t *dst) const;  // dst holds dst_w*dst_h*3 bytes

  int out_x() const { return ox_; }
  int out_y() const { return oy_; }
  int out_w() const { return ow_; }
  int out_h() const { return oh_; }

 private:
  struct Span {
    uint16_t begin, end;  // source pixel range [begin, end) for one destination column/row
  };
  std::vector<Span> cols_, rows_;
  int src_w_ = 0, src_h_ = 0, dst_w_ = 0, dst_h_ = 0;
  int ox_ = 0, oy_ = 0, ow_ = 0, oh_ = 0;
};

}  // namespace p64
