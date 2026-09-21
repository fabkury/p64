#!/usr/bin/env python3
"""Draws the schematics and connection maps for docs/hardware/encoders-*.md.

Run from the repo root with the system Python (needs `pip install schemdraw`;
matplotlib is only needed for the optional PNG previews):

    python docs/hardware/tools/draw_encoders.py            # SVGs into docs/hardware/img/
    python docs/hardware/tools/draw_encoders.py --png      # also PNG previews

Sources of the numbers drawn here: the Waveshare ESP32-S3-RGB-Matrix schematic
(R59/R60 10 k pull-downs on IO45/IO46, the 4-pin GPIO socket U8), Adafruit's
5880 guide (10 k pull-ups, jumpers A0/A1/A2, switch on seesaw pin 24), the
SparkFun Qwiic Twist hookup guide (2.2 k pull-ups, address 0x3F), the ESP32-S3
datasheet (VIH min = 0.75 VDD).

schemdraw places an Ic's side pins bottom-up, so `ic()` reverses the lists it
is given top to bottom.
"""
import sys
from pathlib import Path

import schemdraw
import schemdraw.elements as elm

OUT = Path(__file__).resolve().parents[1] / "img"
PNG = "--png" in sys.argv

# Wire colours of the STEMMA QT / Qwiic cables (Adafruit 4209, 4397, 4399).
BLACK, RED, BLUE, YELLOW = "#222222", "#c62828", "#1565c0", "#c9a400"
GREY = "#777777"
SP = 2.2  # pin spacing


def save(d: schemdraw.Drawing, name: str) -> None:
    d.save(str(OUT / f"{name}.svg"))
    if PNG:
        d.save(str(OUT / f"{name}.png"), dpi=110)
    print("wrote", name)


def ic(pins_left, pins_right, label, **kw):
    """An Ic with `pins_left` / `pins_right` given top to bottom as (name, anchor[, pin])."""
    pins = []
    for spec in reversed(pins_left):
        pins.append(elm.IcPin(name=spec[0], side="left", anchorname=spec[1], pin=spec[2] if len(spec) > 2 else None))
    for spec in reversed(pins_right):
        pins.append(elm.IcPin(name=spec[0], side="right", anchorname=spec[1], pin=spec[2] if len(spec) > 2 else None))
    kw.setdefault("pinspacing", SP)
    kw.setdefault("edgepadW", 2.2)
    kw.setdefault("edgepadH", 0.6)
    kw.setdefault("leadlen", 0.8)
    return elm.Ic(pins=pins, label=label, **kw).theta(0)


def controller_box(label: str) -> elm.Ic:
    return ic([], [("IO45", "sda", "1"), ("IO46", "scl", "2"), ("3V3", "v33", "3"), ("GND", "gnd", "4")], label)


def board_box(label: str) -> elm.Ic:
    return ic([("SDA", "sda"), ("SCL", "scl"), ("V+", "v33"), ("GND", "gnd")],
              [("SDA", "sda2"), ("SCL", "scl2"), ("V+", "v332"), ("GND", "gnd2")], label)


def bus_schematic(name: str, boards, pullups: bool, title: str, note: str) -> None:
    """Controller -> (pull-ups) -> board 1 [-> board 2]. `boards` is a list of
    (label, internal pull-up text)."""
    with schemdraw.Drawing(show=False) as d:
        d.config(unit=2, fontsize=11)
        ctl = d.add(controller_box("ESP32-S3-RGB-Matrix\nGPIO socket U8\n(JST-SH 1.0 mm, 4 pins)"))
        # The controller's own pull-downs, drawn next to the pins (they cross the
        # nets below without a dot: no connection).
        for pin, rname in (("sda", "R59 10k"), ("scl", "R60 10k")):
            p = getattr(ctl, pin)
            d.add(elm.Line().at(p).right(0.9))
            n = d.add(elm.Dot())
            d.add(elm.Resistor().at(n.center).down(1.3).label(rname, loc="bottom", fontsize=8, ofst=(0.1, 0)).color(GREY))
            d.add(elm.Ground().color(GREY))
        d.add(elm.Line().at(ctl.v33).right(0.9))
        d.add(elm.Line().at(ctl.gnd).right(0.9))
        d.add(elm.Dot())
        d.add(elm.Ground().color(GREY))
        x0 = ctl.sda[0] + 0.9
        # Socket boundary (dashed) and the harness.
        xs = x0 + 1.8
        d.add(elm.Line().at((xs, ctl.sda[1] + 1.4)).to((xs, ctl.gnd[1] - 1.2)).linestyle("--").color(GREY))
        d.add(elm.Label().at((xs, ctl.sda[1] + 1.6)).label("socket / plug", fontsize=8, color=GREY))
        xr = x0 + 3.6  # where the external pull-ups hang
        xb = xr + 2.6  # where board 1 starts
        for pin, colour in (("sda", BLUE), ("scl", YELLOW), ("v33", RED), ("gnd", BLACK)):
            p = getattr(ctl, pin)
            d.add(elm.Line().at((x0, p[1])).to((xb, p[1])).color(colour))
        if pullups:
            for pin, dx, up in (("sda", 0.0, 1.7), ("scl", 1.3, 1.7 + SP)):
                p = getattr(ctl, pin)
                d.add(elm.Dot().at((xr + dx, p[1])))
                d.add(elm.Resistor().at((xr + dx, p[1])).up(up).label("2.2k", loc="bottom", fontsize=8, ofst=(0.1, 0)))
                d.add(elm.Vdd().label("3V3", fontsize=8))
            d.add(elm.Label().at((xr + 0.65, ctl.gnd[1] - 1.0)).label("your 2.2k pull-ups\n(breadboard or soldered)", fontsize=8, color=GREY))
        x = xb
        for i, (label, pull_text) in enumerate(boards):
            b = d.add(board_box(label).at((x, ctl.sda[1])).anchor("sda"))
            d.add(elm.Label().at((b.center[0], b.gnd[1] - 1.0)).label(pull_text, fontsize=8, color=GREY))
            if i + 1 < len(boards):
                for pin, colour in (("sda2", BLUE), ("scl2", YELLOW), ("v332", RED), ("gnd2", BLACK)):
                    p = getattr(b, pin)
                    d.add(elm.Line().at(p).right(1.8).color(colour))
                d.add(elm.Label().at((b.sda2[0] + 0.9, b.sda2[1] + 0.9)).label("4399 cable\n50 mm", fontsize=8, color=GREY))
                x = b.sda2[0] + 1.8
        d.add(elm.Label().at((ctl.sda[0] - 5.0, ctl.sda[1] + 2.6)).label(title, fontsize=13, halign="left"))
        d.add(elm.Label().at((ctl.sda[0] - 5.0, ctl.gnd[1] - 2.6)).label(note, fontsize=8, color=GREY, halign="left"))
        save(d, name)


def bench_map(name: str, second_board: bool) -> None:
    """Breadboard connection map: 4209 pins -> rows/rails -> 4397 sockets."""
    with schemdraw.Drawing(show=False) as d:
        d.config(unit=2, fontsize=10)
        left = d.add(ic([], [
            ("black pin  = IO45 (SDA)", "p1"),
            ("red pin    = IO46 (SCL)", "p2"),
            ("blue pin   = 3V3", "p3"),
            ("yellow pin = GND", "p4")],
            "Adafruit 4209\nSH plug in the controller's\nGPIO socket (pins 1..4),\nmale pins into the breadboard", edgepadW=2.6))
        bb = d.add(ic(
            [("row 5", "r5"), ("row 10", "r10"), ("+ rail", "vp"), ("- rail", "vm")],
            [("row 5", "r5b"), ("row 10", "r10b"), ("+ rail", "vpb"), ("- rail", "vmb")],
            "breadboard").at((left.p1[0] + 3.2, left.p1[1])).anchor("r5"))
        for a, b, colour in (("p1", "r5", BLACK), ("p2", "r10", RED), ("p3", "vp", BLUE), ("p4", "vm", YELLOW)):
            d.add(elm.Line().at(getattr(left, a)).to(getattr(bb, b)).color(colour))
        # Pull-ups: row 5 -> + rail and row 10 -> + rail, drawn above the breadboard box.
        top = bb.r5[1] + 1.2
        for x, txt, loc in ((bb.center[0] - 1.3, "2.2k\nrow 5 to + rail", "top"), (bb.center[0] + 1.3, "2.2k\nrow 10 to + rail", "bottom")):
            d.add(elm.Resistor().at((x, top)).up(1.8).label(txt, fontsize=8, loc=loc))
        d.add(elm.Label().at((bb.center[0], top - 0.25)).label("(each resistor plugs into a row and into the + rail)", fontsize=7, color=GREY))
        right = d.add(ic([
            ("blue socket   = SDA", "sda"), ("yellow socket = SCL", "scl"), ("red socket    = V+", "v33"), ("black socket  = GND", "gnd")], [],
            "Adafruit 4397\nfemale sockets on header pins\nin the breadboard,\nSH plug in board 1", edgepadW=2.6
            ).at((bb.r5b[0] + 3.2, bb.r5b[1])).anchor("sda"))
        for a, b, colour in (("r5b", "sda", BLUE), ("r10b", "scl", YELLOW), ("vpb", "v33", RED), ("vmb", "gnd", BLACK)):
            d.add(elm.Line().at(getattr(bb, a)).to(getattr(right, b)).color(colour))
        if second_board:
            d.add(elm.Label().at((right.center[0], right.gnd[1] - 1.4)).label(
                "board 1 (0x36)  --4399, 50 mm-->  board 2 (0x37, A0 bridged)", fontsize=9, color=GREY))
        d.add(elm.Label().at((left.center[0] - 2.0, left.p4[1] - 1.6)).label(
            "Wire colours are the cables' own. On the 4209 the colours do NOT mean GND / V+ / SDA / SCL:\n"
            "the Waveshare socket has a different pin order. Connect by function, as drawn.",
            fontsize=8, color=GREY, halign="left"))
        save(d, name)


def harness_map(name: str) -> None:
    """The in-shell harness without a breadboard: 4209 pins mated to 4397 sockets."""
    with schemdraw.Drawing(show=False) as d:
        d.config(unit=2, fontsize=10)
        left = d.add(ic([], [
            ("black pin  (IO45 = SDA)", "p1"), ("red pin    (IO46 = SCL)", "p2"),
            ("blue pin   (3V3)", "p3"), ("yellow pin (GND)", "p4")],
            "Adafruit 4209\n(from the controller)", edgepadW=1.6))
        right = d.add(ic([
            ("blue socket   (SDA)", "sda"), ("yellow socket (SCL)", "scl"),
            ("red socket    (V+)", "v33"), ("black socket  (GND)", "gnd")], [],
            "Adafruit 4397\n(to the encoder board)", edgepadW=1.6).at((left.p1[0] + 4.5, left.p1[1])).anchor("sda"))
        for a, b, colour in (("p1", "sda", BLUE), ("p2", "scl", YELLOW), ("p3", "v33", RED), ("p4", "gnd", BLACK)):
            d.add(elm.Line().at(getattr(left, a)).to(getattr(right, b)).color(colour))
        d.add(elm.Label().at((left.center[0] - 1.0, left.p4[1] - 1.6)).label(
            "Each male pin pushes into one female socket; wrap each pair in tape or heat-shrink.\n"
            "Line colour = function (blue SDA, yellow SCL, red V+, black GND), not the wire colour.",
            fontsize=8, color=GREY, halign="left"))
        save(d, name)


def divider(name: str) -> None:
    """The idle-level divider: R_up (board pull-ups || external) against R59/R60."""
    with schemdraw.Drawing(show=False) as d:
        d.config(unit=2, fontsize=10)
        d.add(elm.Vdd().label("3V3"))
        d.add(elm.Resistor().down().label("R_up\n(all pull-ups\nin parallel)", loc="bottom"))
        n = d.add(elm.Dot())
        d.add(elm.Line().right(1.5).at(n.center))
        d.add(elm.Label().label("V_idle = 3.3 V x 10k / (10k + R_up)", halign="left", fontsize=10))
        d.add(elm.Resistor().at(n.center).down().label("R59 / R60\n10k", loc="bottom"))
        d.add(elm.Ground())
        d.add(elm.Label().at((1.5, -3.0)).label("ESP32-S3 reads HIGH only above 0.75 x 3.3 V = 2.475 V", halign="left", fontsize=9, color=GREY))
        save(d, name)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    bus_schematic(
        "a-bus-one-board",
        [("Adafruit 5880\nseesaw, address 0x36", "on-board pull-ups 10k to V+")],
        pullups=True,
        title="Option A: one encoder on the second I2C bus (IO45 = SDA, IO46 = SCL)",
        note="Lines that cross without a dot are not connected. R59/R60 are on the controller; the 2.2k are yours.")
    bus_schematic(
        "b-bus-two-boards",
        [("Adafruit 5880, board 1\naddress 0x36", "10k pull-ups"),
         ("Adafruit 5880, board 2\naddress 0x37 (A0 bridged)", "10k pull-ups")],
        pullups=True,
        title="Option B: two encoders chained on the second I2C bus",
        note="Board 2 differs from board 1 only by the solder bridge on jumper A0 (back of the board).")
    bus_schematic(
        "a-bus-twist",
        [("Adafruit 5880\naddress 0x36", "10k pull-ups"),
         ("SparkFun Qwiic Twist\naddress 0x3F", "2.2k pull-ups (its own)")],
        pullups=False,
        title="Option A appendix: 5880 + Qwiic Twist, no external resistors",
        note="The Twist's own 2.2k pull-ups replace the breadboard resistors; the harness is then cable-to-cable.")
    bench_map("bench-map-one-board", second_board=False)
    bench_map("bench-map-two-boards", second_board=True)
    harness_map("harness-cable-to-cable")
    divider("idle-level")


if __name__ == "__main__":
    main()
