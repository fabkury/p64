#!/usr/bin/env python3
r"""Send pixels to a p64 over UDP: DDP (port 4048) or the raw p64 format (port 4064).

    python tools\stream_send.py p64.local test                 # moving test pattern, raw, 64x64
    python tools\stream_send.py p64.local test --ddp --size 128
    python tools\stream_send.py p64.local image.gif            # any file Pillow opens (animations loop)
    python tools\stream_send.py p64.local image.png --fmt 565
    python tools\stream_send.py p64.local screen               # the primary screen, scaled (needs mss)

Options: --ddp (DDP instead of raw), --size N (frame side for test/screen, 1..128; DDP
takes 64 or 128), --fps N (test/screen rate, default 30), --fmt 888|565|indexed (raw pixel
format; indexed quantises to 256 colours), --seconds N (stop after N s), --port N.

Raw p64 frame (little-endian): "P64F", u8 version 1, u8 format (0 RGB888, 1 RGB565,
2 indexed 8-bit), u16 width, u16 height, u16 sequence, u8 flags (1 palette present,
2 last chunk), u8 reserved, u32 offset, u32 total; then up to 1400 bytes of the frame's
stream (the 768-byte RGB888 palette first when indexed, then the pixels).
"""

import argparse
import math
import socket
import struct
import sys
import time

DDP_PORT = 4048
RAW_PORT = 4064
RAW_CHUNK = 1400
DDP_CHUNK = 1440


def raw_datagrams(pixels, width, height, fmt, sequence, palette=None):
    stream = (palette or b"") + pixels
    total = len(stream)
    flags = 1 if palette else 0
    out = []
    for offset in range(0, total, RAW_CHUNK):
        chunk = stream[offset:offset + RAW_CHUNK]
        last = offset + len(chunk) >= total
        head = struct.pack("<4sBBHHHBBII", b"P64F", 1, fmt, width, height, sequence & 0xFFFF,
                           flags | (2 if last else 0), 0, offset, total)
        out.append(head + chunk)
    return out


def ddp_datagrams(rgb, sequence):
    out = []
    total = len(rgb)
    for offset in range(0, total, DDP_CHUNK):
        chunk = rgb[offset:offset + DDP_CHUNK]
        push = offset + len(chunk) >= total
        head = struct.pack(">BBBBIH", 0x40 | (1 if push else 0), sequence & 0x0F, 1, 1, offset, len(chunk))
        out.append(head + chunk)
    return out


def to_rgb565(rgb):
    out = bytearray(len(rgb) // 3 * 2)
    for i in range(0, len(rgb), 3):
        r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out[i // 3 * 2] = v & 0xFF
        out[i // 3 * 2 + 1] = v >> 8
    return bytes(out)


def to_indexed(image):
    """Quantises a Pillow image to 256 colours: (indexes, 768-byte palette)."""
    q = image.convert("RGB").quantize(colors=256)
    pal = q.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    return q.tobytes(), bytes(pal)


def test_frame(side, t):
    """A moving pattern: a colour wheel gradient with a bouncing white square and a frame counter bar."""
    px = bytearray(side * side * 3)
    cx = side / 2 + math.cos(t * 1.3) * side / 3
    cy = side / 2 + math.sin(t * 0.9) * side / 3
    for y in range(side):
        for x in range(side):
            i = (y * side + x) * 3
            h = ((x + y) / (2 * side) + t * 0.1) % 1.0
            r, g, b = hsv(h, 1.0, 0.6)
            if abs(x - cx) < side / 12 and abs(y - cy) < side / 12:
                r = g = b = 255
            px[i], px[i + 1], px[i + 2] = r, g, b
    bar = int((t * 20) % side)
    for x in range(bar):
        i = ((side - 1) * side + x) * 3
        px[i], px[i + 1], px[i + 2] = 255, 255, 255
    return bytes(px)


def hsv(h, s, v):
    i = int(h * 6) % 6
    f = h * 6 - int(h * 6)
    p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
    r, g, b = [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i]
    return int(r * 255), int(g * 255), int(b * 255)


class Sender:
    def __init__(self, host, port, ddp, fmt):
        self.addr = (host, port or (DDP_PORT if ddp else RAW_PORT))
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.ddp = ddp
        self.fmt = fmt
        self.sequence = 0
        self.frames = 0
        self.bytes = 0

    def send_rgb(self, rgb, width, height, image=None):
        if self.ddp:
            grams = ddp_datagrams(rgb, self.sequence)
        elif self.fmt == "565":
            grams = raw_datagrams(to_rgb565(rgb), width, height, 1, self.sequence)
        elif self.fmt == "indexed":
            if image is None:
                from PIL import Image
                image = Image.frombytes("RGB", (width, height), rgb)
            idx, pal = to_indexed(image)
            grams = raw_datagrams(idx, width, height, 2, self.sequence, pal)
        else:
            grams = raw_datagrams(rgb, width, height, 0, self.sequence)
        for g in grams:
            self.sock.sendto(g, self.addr)
            self.bytes += len(g)
        self.sequence += 1
        self.frames += 1


def run_test(sender, side, fps, seconds):
    t0 = time.time()
    n = 0
    while seconds <= 0 or time.time() - t0 < seconds:
        t = time.time() - t0
        sender.send_rgb(test_frame(side, t), side, side)
        n += 1
        target = t0 + n / fps
        delay = target - time.time()
        if delay > 0:
            time.sleep(delay)
    return n


def run_file(sender, path, size, fps, seconds):
    from PIL import Image, ImageSequence
    im = Image.open(path)
    frames = []
    for f in ImageSequence.Iterator(im):
        rgb = f.convert("RGBA")
        bg = Image.new("RGBA", rgb.size, (0, 0, 0, 255))
        bg.alpha_composite(rgb)
        w, h = bg.size
        if w > 128 or h > 128:
            scale = 128 / max(w, h)
            bg = bg.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.LANCZOS)
        if sender.ddp:
            bg = bg.resize((size, size), Image.LANCZOS)
        frames.append((bg.convert("RGB"), f.info.get("duration", 1000 // fps)))
    t0 = time.time()
    while seconds <= 0 or time.time() - t0 < seconds:
        for img, delay in frames:
            sender.send_rgb(img.tobytes(), img.width, img.height, img)
            time.sleep(max(delay, 20) / 1000)
            if seconds > 0 and time.time() - t0 >= seconds:
                break
        if len(frames) == 1 and seconds <= 0:
            time.sleep(0.5)  # a still image: re-send twice a second so the panel keeps it


def run_screen(sender, side, fps, seconds):
    import mss
    from PIL import Image
    with mss.mss() as sct:
        mon = sct.monitors[1]
        t0 = time.time()
        n = 0
        while seconds <= 0 or time.time() - t0 < seconds:
            shot = sct.grab(mon)
            img = Image.frombytes("RGB", shot.size, shot.bgra, "raw", "BGRX")
            s = min(shot.size)
            img = img.crop(((shot.size[0] - s) // 2, (shot.size[1] - s) // 2, (shot.size[0] + s) // 2, (shot.size[1] + s) // 2))
            img = img.resize((side, side), Image.BOX)
            sender.send_rgb(img.tobytes(), side, side, img)
            n += 1
            target = t0 + n / fps
            delay = target - time.time()
            if delay > 0:
                time.sleep(delay)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("what", help="test | screen | <image file>")
    ap.add_argument("--ddp", action="store_true")
    ap.add_argument("--size", type=int, default=64)
    ap.add_argument("--fps", type=float, default=30)
    ap.add_argument("--fmt", choices=("888", "565", "indexed"), default="888")
    ap.add_argument("--seconds", type=float, default=0)
    ap.add_argument("--port", type=int, default=0)
    a = ap.parse_args()
    if a.ddp and a.size not in (64, 128):
        sys.exit("DDP frames are 64x64 or 128x128")
    if not 1 <= a.size <= 128:
        sys.exit("size is 1..128")
    sender = Sender(a.host, a.port, a.ddp, a.fmt)
    t0 = time.time()
    try:
        if a.what == "test":
            run_test(sender, a.size, a.fps, a.seconds)
        elif a.what == "screen":
            run_screen(sender, a.size, a.fps, a.seconds)
        else:
            run_file(sender, a.what, a.size, a.fps, a.seconds)
    except KeyboardInterrupt:
        pass
    dt = max(time.time() - t0, 1e-6)
    print("%d frames, %.1f fps, %.0f KB/s" % (sender.frames, sender.frames / dt, sender.bytes / dt / 1024))


if __name__ == "__main__":
    main()
