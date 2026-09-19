#!/usr/bin/env python3
r"""Device smoke test of the streams (M8): raw p64 UDP and DDP, takeover and silence.

    python tests\device\stream_smoke.py [http://p64.local]

Sends known patterns over both protocols from this PC and checks, through the HTTP API,
that the stream takes the panel, that the panel frame (/api/v1/frame.raw) matches the
pattern pixel for pixel (64x64 RGB888 and indexed; 32x32 RGB565 upscaled; 128x128
box-downscaled; chunks sent in reverse order), the counters and the latency, the return
to the show after the silence timeout, takeover off, and the Stream main state. Puts
the settings back. Exit code 1 on any failure.
"""

import os
import socket
import sys
import time
import urllib.parse

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from api_smoke import check, request  # noqa: E402
from stream_send import DDP_PORT, RAW_PORT, ddp_datagrams, raw_datagrams, to_rgb565  # noqa: E402


def status(base):
    st, j = request(base, "GET", "/api/v1/status")
    assert st == 200, j
    return j["data"]


def frame(base):
    st, r = request(base, "GET", "/api/v1/frame.raw", raw=True)
    assert st == 200 and len(r) == 64 * 64 * 3
    return r


def settings(base, patch):
    st, j = request(base, "PUT", "/api/v1/settings", patch)
    assert st == 200, j
    return j["data"]


def pattern(side, block=1):
    """(x, y) -> (X*4 & 255, Y*4 & 255, (X+Y) & 255) with X = x // block: uniform block x block cells."""
    px = bytearray(side * side * 3)
    for y in range(side):
        for x in range(side):
            X, Y = x // block, y // block
            i = (y * side + x) * 3
            px[i], px[i + 1], px[i + 2] = (X * 4) & 255, (Y * 4) & 255, (X + Y) & 255
    return bytes(px)


def expand565(rgb):
    out = bytearray(len(rgb))
    for i in range(0, len(rgb), 3):
        r5, g6, b5 = rgb[i] >> 3, rgb[i + 1] >> 2, rgb[i + 2] >> 3
        out[i], out[i + 1], out[i + 2] = (r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)
    return bytes(out)


def upscale(rgb, side, factor):
    out = bytearray(side * factor * side * factor * 3)
    big = side * factor
    for y in range(big):
        for x in range(big):
            i = ((y // factor) * side + x // factor) * 3
            o = (y * big + x) * 3
            out[o:o + 3] = rgb[i:i + 3]
    return bytes(out)


def downscale(rgb, side, factor):
    """Exact for patterns uniform per factor x factor block."""
    small = side // factor
    out = bytearray(small * small * 3)
    for y in range(small):
        for x in range(small):
            i = ((y * factor) * side + x * factor) * 3
            o = (y * small + x) * 3
            out[o:o + 3] = rgb[i:i + 3]
    return bytes(out)


def diff_count(a, b):
    return sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3])


class Udp:
    def __init__(self, host):
        self.host = host
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send(self, grams, port, gap=0.0):
        for g in grams:
            self.sock.sendto(g, (self.host, port))
            if gap:
                time.sleep(gap)


def send_raw(udp, rgb, w, h, fmt=0, seq=0, palette=None, reverse=False, repeat=1, fps=20):
    data = rgb if fmt != 1 else to_rgb565(rgb)
    for n in range(repeat):
        grams = raw_datagrams(data, w, h, fmt, seq + n, palette)
        if reverse:
            grams = list(reversed(grams))
        udp.send(grams, RAW_PORT)
        if repeat > 1:
            time.sleep(1.0 / fps)


def send_ddp(udp, rgb, seq=0, repeat=1, fps=20):
    for n in range(repeat):
        udp.send(ddp_datagrams(rgb, seq + n), DDP_PORT)
        if repeat > 1:
            time.sleep(1.0 / fps)


def wait_for(base, pred, seconds):
    deadline = time.time() + seconds
    d = status(base)
    while time.time() < deadline and not pred(d):
        time.sleep(0.3)
        d = status(base)
    return d


def expect_frame(base, expected, what, tolerance=0):
    got = frame(base)
    bad = diff_count(got, expected)
    check(bad <= tolerance, "%s: %d of 4096 pixels differ" % (what, bad))


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    host = urllib.parse.urlparse(base).hostname
    udp = Udp(socket.gethostbyname(host))
    original = request(base, "GET", "/api/v1/settings")[1]["data"]
    silence_ms = 1500
    settings(base, {"show": {"main_state": "animation_show"},
                    "stream": {"takeover": True, "silence_ms": silence_ms, "ddp_enabled": True, "raw_udp_enabled": True}})
    time.sleep(1.5)
    d = status(base)
    check(d["stream"]["ddp_listening"] and d["stream"]["raw_listening"], "both listeners are up")
    check(not d["stream"]["active"] and not d["playback"]["stream_up"], "no stream to begin with")
    history_before = len(request(base, "GET", "/api/v1/history")[1]["data"]["items"])
    frames0 = d["stream"]["frames"]

    # Raw RGB888 64x64: pixel-exact on the panel; the stream takes the panel.
    p64 = pattern(64)
    send_raw(udp, p64, 64, 64, repeat=10)
    d = wait_for(base, lambda d: d["playback"]["stream_up"], 3)
    check(d["playback"]["stream_up"] and d["stream"]["active"], "raw stream took the panel")
    check(d["stream"]["protocol"] == "raw" and d["stream"]["width"] == 64 and d["stream"]["height"] == 64, "status names raw 64x64")
    check(d["stream"]["frames"] - frames0 >= 10, "10 frames counted (%d)" % (d["stream"]["frames"] - frames0))
    check(d["stream"]["last_latency_ms"] < 50, "frame latency %.1f ms" % d["stream"]["last_latency_ms"])
    send_raw(udp, p64, 64, 64, seq=20)
    time.sleep(0.2)
    expect_frame(base, p64, "raw RGB888 64x64 pixel-exact")

    # Chunks in reverse order still assemble.
    p64b = pattern(64)[::-1]
    send_raw(udp, p64b, 64, 64, seq=30, reverse=True)
    time.sleep(0.2)
    expect_frame(base, p64b, "raw frame with chunks reversed")

    # RGB565 at 32x32: upscaled 2x, values bit-replicated.
    p32 = pattern(32)
    send_raw(udp, p32, 32, 32, fmt=1, seq=40)
    time.sleep(0.2)
    expect_frame(base, upscale(expand565(p32), 32, 2), "raw RGB565 32x32 upscaled")
    d = status(base)
    check(d["stream"]["width"] == 32, "status follows the frame size (32)")

    # Indexed 8-bit at 64x64 with a palette.
    palette = bytes(b for i in range(256) for b in (i, 255 - i, i ^ 0x55))
    idx = bytes(((x + y * 4) & 255) for y in range(64) for x in range(64))
    expected = bytes(b for i in idx for b in palette[i * 3:i * 3 + 3])
    send_raw(udp, idx, 64, 64, fmt=2, seq=50, palette=palette)
    time.sleep(0.2)
    expect_frame(base, expected, "raw indexed 64x64 with palette")

    d = status(base)
    check(d["stream"]["incomplete"] == 0, "no incomplete frame so far (%d)" % d["stream"]["incomplete"])

    # 128x128 RGB888: 49152 bytes in 36 chunks, box-downscaled 2:1 (uniform 2x2 cells: exact).
    p128 = pattern(128, block=2)
    send_raw(udp, p128, 128, 128, seq=60, repeat=3)
    time.sleep(0.2)
    expect_frame(base, downscale(p128, 128, 2), "raw RGB888 128x128 downscaled")
    inc128 = status(base)["stream"]["incomplete"]
    print("     incomplete after three 36-chunk bursts: %d" % inc128)

    # DDP 64x64 and 128x128.
    send_ddp(udp, p64, seq=1, repeat=5)
    d = wait_for(base, lambda d: d["stream"]["protocol"] == "ddp", 3)
    check(d["stream"]["protocol"] == "ddp" and d["stream"]["width"] == 64, "DDP 64x64 received")
    time.sleep(0.2)
    expect_frame(base, p64, "DDP 64x64 pixel-exact")
    send_ddp(udp, p128, seq=6, repeat=3)
    time.sleep(0.3)
    d = status(base)
    check(d["stream"]["width"] == 128, "DDP 128x128 received")
    expect_frame(base, downscale(p128, 128, 2), "DDP 128x128 downscaled")

    # Rate: 30 fps for 2 s.
    t0 = time.time()
    n = 0
    while time.time() - t0 < 2.0:
        send_raw(udp, p64, 64, 64, seq=100 + n)
        n += 1
        time.sleep(max(0, t0 + n / 30 - time.time()))
    d = status(base)
    check(d["stream"]["fps"] >= 25, "30 fps stream measured at %.1f fps" % d["stream"]["fps"])
    check(d["stream"]["rejected"] == 0, "no rejected datagrams (%d)" % d["stream"]["rejected"])
    print("     incomplete after the 128x128 DDP bursts and 2 s at 30 fps: %d" % d["stream"]["incomplete"])
    check(d["stream"]["incomplete"] - inc128 <= 2, "at most 2 incomplete frames in the 30 fps run and the DDP bursts (%d)" % (d["stream"]["incomplete"] - inc128))

    # Silence: the show takes the panel back and the stream did not enter history.
    last = frame(base)
    d = wait_for(base, lambda d: not d["playback"]["stream_up"], silence_ms / 1000 + 3)
    check(not d["playback"]["stream_up"] and not d["stream"]["active"], "the stream released the panel after the silence timeout")
    time.sleep(0.5)
    check(frame(base) != last, "the panel shows the show again")
    history_after = len(request(base, "GET", "/api/v1/history")[1]["data"]["items"])
    check(history_after >= history_before, "history kept (%d -> %d)" % (history_before, history_after))
    check(not any(i["kind"] == "stream" for i in request(base, "GET", "/api/v1/history")[1]["data"]["items"]), "no stream item in history")

    # Takeover off: frames are counted but the panel stays with the show.
    settings(base, {"stream": {"takeover": False}})
    time.sleep(0.5)
    before = status(base)["stream"]["frames"]
    send_raw(udp, p64, 64, 64, seq=200, repeat=5)
    time.sleep(0.5)
    d = status(base)
    check(d["stream"]["frames"] - before >= 5 and d["stream"]["active"], "frames counted with takeover off")
    check(not d["playback"]["stream_up"], "panel not taken with takeover off")
    d = wait_for(base, lambda d: not d["stream"]["active"], silence_ms / 1000 + 3)

    # The Stream main state: waiting screen, then the stream, then the waiting screen.
    settings(base, {"show": {"main_state": "stream"}})
    time.sleep(1)
    d = status(base)
    check(d["playback"]["state"] == "stream" and not d["playback"]["stream_up"], "Stream state waits")
    waiting = frame(base)
    send_raw(udp, p64, 64, 64, seq=300, repeat=5)
    d = wait_for(base, lambda d: d["playback"]["stream_up"], 3)
    check(d["playback"]["stream_up"], "stream shown in the Stream state even with takeover off")
    time.sleep(0.2)
    expect_frame(base, p64, "Stream state frame pixel-exact")
    d = wait_for(base, lambda d: not d["playback"]["stream_up"], silence_ms / 1000 + 3)
    check(not d["playback"]["stream_up"] and d["playback"]["state"] == "stream", "back to waiting after silence")
    time.sleep(0.5)
    check(frame(base) == waiting, "waiting screen back")

    # Back to what it was.
    settings(base, {"show": {"main_state": original["show"]["main_state"]},
                    "stream": {"takeover": original["stream"]["takeover"], "silence_ms": original["stream"]["silence_ms"]}})
    from api_smoke import failures
    print("stream smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
