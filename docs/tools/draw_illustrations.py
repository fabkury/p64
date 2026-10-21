"""Generate documentation SVGs. Run with Python + Pillow; no firmware outputs change."""
from pathlib import Path
from html import escape
import importlib.util
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/images/illustrations'
OUT.mkdir(parents=True, exist_ok=True)

def text(x, y, value, size=20, anchor='middle'):
    return f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}">{escape(value)}</text>'

def box(x, y, w, label, detail=''):
    return (f'<rect x="{x}" y="{y}" width="{w}" height="82" rx="8" fill="#eff6fb" stroke="#8aa6bb"/>'
            + text(x+w/2, y+32, label) + text(x+w/2, y+59, detail, 16))

def arrow(x1, y1, x2, y2):
    return f'<path d="M{x1} {y1} L{x2} {y2}" fill="none" stroke="#24618a" stroke-width="2" marker-end="url(#arrow)"/>'

def save(name, title, desc, h, body):
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="900" height="{h}" viewBox="0 0 900 {h}" role="img" aria-labelledby="title desc">
<title id="title">{escape(title)}</title><desc id="desc">{escape(desc)}</desc>
<defs><marker id="arrow" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0 0 L8 4 L0 8" fill="#24618a"/></marker></defs>
<rect width="100%" height="100%" fill="#fff"/>
<g font-family="Arial, Helvetica, sans-serif" fill="#182c3d">{text(24,38,title,25,'start')}{body}</g></svg>
'''
    (OUT / (name + '.svg')).write_text(svg, encoding='utf-8')

save('playsets', 'Channels → playset → artwork', 'A playset mixes channels by weight. A widget interlude replaces one artwork slot.', 285,
     box(24,72,220,'Local folder','weight 2') + box(24,180,220,'Promoted','weight 1')
     + arrow(244,113,335,153) + arrow(244,221,335,163)
     + box(345,117,210,'Your playset','weighted channel picks')
     + arrow(555,158,625,158) + box(635,117,240,'Artwork slot','or a widget interlude'))

save('stream', 'Live pixels over Wi-Fi', 'A computer sends DDP on UDP 4048 or raw p64 on UDP 4064. Silence timeout returns to the previous mode.', 235,
     box(24,82,215,'Computer','file, test or screen') + arrow(239,123,645,123)
     + text(442,90,'DDP · UDP 4048',18) + text(442,154,'raw p64 · UDP 4064',18)
     + box(655,82,220,'p64','stream takeover') + text(450,208,'Silence timeout → previous mode',18))

save('frame-flow', 'From source to LEDs', 'Core 0 loads file bytes. Core 1 player decodes, scales and overlays. The ready ring passes frames to the renderer; HUB75 DMA drives the panel.', 350,
     box(24,80,235,'Core 0 · loader','file bytes in PSRAM') + arrow(259,121,315,121)
     + box(325,80,245,'Core 1 · player','decode, scale, overlay') + arrow(570,121,625,121)
     + box(635,80,240,'Ready ring','frame + due time') + arrow(755,162,755,225)
     + box(635,235,240,'Core 1 · renderer','rotation, gains, present') + arrow(635,276,580,276)
     + box(325,235,245,'HUB75 DMA','continuous panel refresh')
     + text(24,208,'Widgets and streams also',17,'start') + text(24,234,'supply a FrameSource.',17,'start'))

save('rear-ports', 'USB-C ports · viewed from behind', 'On the assembled shell POWER is left and USB is right. A groove beside USB identifies the programming port. Not to scale.', 285,
     '<rect x="170" y="70" width="560" height="110" rx="12" fill="#f5f7f9" stroke="#8aa6bb"/>'
     '<rect x="250" y="102" width="140" height="42" rx="21" fill="#182c3d"/>'
     '<rect x="495" y="102" width="140" height="42" rx="21" fill="#182c3d"/>'
     '<path d="M668 105 V141" stroke="#24618a" stroke-width="5"/>'
     + text(320,211,'POWER') + text(320,239,'5 V supply',17)
     + text(565,211,'USB') + text(565,239,'programming / data',17)
     + text(750,113,'groove',17) + text(450,270,'Schematic · not to scale',14))

spec = importlib.util.spec_from_file_location('p64_clock_mock', ROOT / 'firmware/tools/mock_clock_faces.py')
mock = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = mock
spec.loader.exec_module(mock)
items = [(name, mock.render_reference(name, mock.Moment(10,32,37), 's')) for name in mock.FACES]
items.append(('LED · VFD', mock.render_reference('led', mock.Moment(10,32,37), 'sv')))
body = ''
for i, (name, frame) in enumerate(items):
    x, y = 28 + (i % 4)*218, 72 + (i // 4)*238
    body += f'<g transform="translate({x} {y})" shape-rendering="crispEdges">'
    # Run-length encode rows, preserving the reference renderer's exact pixels.
    for py in range(64):
        px = 0
        while px < 64:
            rgb = frame.img.getpixel((px,py))
            end = px + 1
            while end < 64 and frame.img.getpixel((end,py)) == rgb:
                end += 1
            r,g,b = rgb
            body += f'<rect x="{px*3}" y="{py*3}" width="{(end-px)*3}" height="3" fill="#{r:02x}{g:02x}{b:02x}"/>'
            px = end
    label = {'led':'LED', 'horizon_rd':'Horizon-RD', 'LED · VFD':'LED · VFD'}.get(name, name.capitalize())
    body += '</g>' + text(x+96,y+218,label)
save('clock-faces', 'Themed clock faces', 'Flip, Nixie, Words, Hourglass, Orrery, LED, Horizon-RD, Aquarium, Bracket, Station and LED in VFD style. Digital and Analogue are also available.', 790, body)
print('Generated five SVGs in', OUT)
