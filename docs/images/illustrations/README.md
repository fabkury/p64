# Documentation illustrations

Keep visuals technical and minimal. Use real photos for the physical device,
reference renders for clock faces, and diagrams where relationships need explaining.

| Area | Choice |
|---|---|
| Homepage | Nine photos and a playback clip show a clock, artwork, time overlays, and the finished v7a shell. Link to the clock comparison. |
| Owner's guide | Clock comparison for choosing a face; Home screenshot for finding controls; diagrams for playsets and streaming. |
| Build guide | A CAD close-up explains the insert. A port diagram and assembled rear photo establish orientation. |
| Enclosure | Side-by-side rear renders distinguish the variants. The design reference embeds the adapter cutaway. Label CAD views explicitly. |
| Firmware architecture | A frame-flow diagram introduces the detailed pipeline and task tables. |
| API and specification | Keep precise contracts and settings in searchable text and tables; the owner and architecture guides supply the visual overview. |
| Encoder wiring | Retain the existing circuit drawings and wiring tables. A synthetic assembly image would not establish actual wiring. |
| Status, decisions and history | Keep text-focused; decoration adds little and can obscure dated evidence. |

Use only the verified v7a set in the [photo index](../photos/README.md) for current
documentation. v1 photos are deprecated, including the brick-wall analogue clock,
weather and jellyfish images.

## Regenerate

From the repository root, with Python and Pillow installed:

```sh
python docs/tools/draw_illustrations.py
```

This writes the five SVGs in this folder. It does not change firmware assets or test
references. SVGs have a fixed light background, accessible titles and descriptions,
and no external dependencies.

The clock sheet imports `firmware/tools/mock_clock_faces.py`, the project's reference
renderer, and preserves its 64×64 pixels. It shows eleven themed faces plus LED's
VFD style; Digital and Analogue are also supported. These are software previews,
not photographs or measurements of panel colour. Existing clock artwork retains
its [credits](../../credits.md).

Diagram behavior comes from the [owner's guide](../../using-p64.md),
[build guide](../../build-your-own.md), and
[architecture](../../../firmware/docs/architecture.md). Update diagrams with those
documents when behavior changes. CAD images come from the
[enclosure sources and regeneration workflow](../../../enclosure/design-reference.md#regenerating-the-outputs).

## Future photos

Useful additions when the hardware is available: the installed cradle before panel
seating, a close-up of the fully seated adapters on the unmodified frame, and
p64b's finished harness and knobs.
Keep p64b renders labelled until assembly photos exist. New AI-generated product
photos or decorative artwork are unnecessary for this documentation style.
