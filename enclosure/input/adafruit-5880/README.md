# Adafruit I2C QT Rotary Encoder board file (Adafruit 4991 / 5880)

`Adafruit I2C QT Rotary Encoder.brd` is the EagleCAD board file of Adafruit's I2C STEMMA QT
rotary encoder breakout, copied unchanged on 2026-09-22 from
https://github.com/adafruit/Adafruit-I2C-QT-Rotary-Encoder-PCB (commit `4b82c1319140`,
2021-05-11). Adafruit 5880 is this board with the encoder pre-soldered. Licence: Creative
Commons Attribution-ShareAlike 3.0 Unported (`license.txt`, Adafruit's copy), which is
why the file can live here; the encoder's datasheet (Bourns PEC11, Bourns' copyright) is
not stored, only cited: https://cdn-shop.adafruit.com/datasheets/pec11.pdf (rev. 05/11;
the same file sits in Adafruit's repository as `pec11.pdf`).

What the p64 shell takes from the board file (read with a small XML parse of the
`plain` outline, the `MOUNTINGHOLE_2.5_PLATED` elements and the `PEC11+SWITCH` footprint):

| Item | Board file | Shell parameter |
|---|---|---|
| Outline | 25.4 x 25.4 mm, 2.54 mm corner radius (four 90-degree arcs) | `enc_board` 25.4, `enc_board_r` 2.54 |
| Mounting holes | drill 2.5 mm, pad 3.2 mm, at (2.54, 2.54), (22.86, 2.54), (2.54, 22.86), (22.86, 22.86): a 20.32 mm square | `enc_hole_p` 20.32, `enc_hole_d` 2.5 |
| Encoder | footprint `PEC11+SWITCH` at the board centre (12.7, 12.7), rotated 45 degrees (`MR135`) | encoder at the centre, body at 45 degrees |
| STEMMA QT sockets | `JST_SH4` at (2.54, 12.7) and (22.86, 12.7): the middle of the left and right edges | `enc_rot` +-90 turns them up and down in the shell |
| Header | `1X06_ROUND_70` at (12.7, 2.54): the middle of the bottom edge | ends up towards the shell's centre |

Not in the board file: the PCB thickness (1.6 mm assumed, Adafruit's usual) and anything
about the encoder's height, which comes from the datasheet.
