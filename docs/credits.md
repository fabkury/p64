# Credits and licenses

[Home](../README.md) · [Documentation](README.md)

p64's project license is [Apache 2.0](../LICENSE). Included third-party work retains
its own notices and terms; the linked files are the authoritative license texts.

| Component or asset | Credit and source |
|---|---|
| LED driver | esphome's esp-hub75; [license](../firmware/components/esp-hub75/LICENSE), [p64 changes](../firmware/components/esp-hub75/P64-CHANGES.md) |
| GIF decoder | bitbank2's AnimatedGIF; [license](../firmware/components/animatedgif/LICENSE) |
| PNG/APNG decoder | libpng and its APNG work; [license](../firmware/components/libpng/LICENSE), [integration notes](../firmware/components/libpng/README.md) |
| WebP decoder | libwebp; [license](../firmware/components/libwebp/libwebp/COPYING), [integration notes](../firmware/components/libwebp/P64-NOTES.md) |
| Bundled bitmap fonts | VEXED's Capital Hill, Everyday Slight, Standard, Typical, Ample, Vast Black, and High Birth; source and CC BY 4.0 notices in [font folders](../firmware/assets/fonts/) |
| Painted clock assets | Generated through Retro Diffusion; [source records](../firmware/assets/rd-source/README.md) |
| Encoder board reference | Adafruit 5880 CAD; [source and license](../enclosure/input/adafruit-5880/README.md) |

The browser UI and product architecture draw on [p3a](https://github.com/fabkury/p3a).
[Makapix Club](https://makapix.club) supplies community artwork; displayed artwork
belongs to its creators and is not covered by p64's project license. Weather, air
quality and UV data come from Open-Meteo; see the widget sections of the
[specification](spec/p64-spec.md#7-widgets) for data sources and behavior.

Additional fonts kept as source material have their own information files; their
presence does not mean they are compiled into the firmware. The device lists compiled
fonts through `/api/v1/fonts` and provides credits in Settings → System → About.
