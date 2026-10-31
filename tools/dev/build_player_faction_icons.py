"""Generate the player-faction pool icon textures (needs Pillow; not part of the runtime).

src/extension/x4_lan_lab/assets/textures/ui/x4lan/x4lan_player<N>.dds, N = 1..16: a disc in the
slot colour (tools/dev/build_player_factions.py COLOURS) with a dark rim and the slot number drawn
as seven-segment digits (procedural shapes, no font). Referenced as .tga by libraries/icons.xml;
X4 resolves the .dds texture. Run: python tools/dev/build_player_faction_icons.py
"""
import importlib.util
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'src/extension/x4_lan_lab/assets/textures/ui/x4lan'
SIZE = 256
# Segments a..g of a seven-segment digit: (x0, y0, x1, y1) in a 10 x 18 grid.
SEGMENTS = {'a': (1, 0, 9, 2), 'b': (8, 1, 10, 9), 'c': (8, 9, 10, 17), 'd': (1, 16, 9, 18),
            'e': (0, 9, 2, 17), 'f': (0, 1, 2, 9), 'g': (1, 8, 9, 10)}
DIGITS = {'0': 'abcdef', '1': 'bc', '2': 'abged', '3': 'abgcd', '4': 'fgbc', '5': 'afgcd',
          '6': 'afgedc', '7': 'abc', '8': 'abcdefg', '9': 'abcfgd'}


def colours():
    spec = importlib.util.spec_from_file_location('pf', ROOT / 'tools/dev/build_player_factions.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module.COLOURS


def icon(index, colour):
    image = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.ellipse((8, 8, SIZE - 8, SIZE - 8), fill=(20, 20, 24, 255))
    draw.ellipse((20, 20, SIZE - 20, SIZE - 20), fill=colour + (255,))
    text = str(index)
    unit = 7 if len(text) == 1 else 5.5
    width = len(text) * 10 * unit + (len(text) - 1) * 3 * unit
    x0, y0 = (SIZE - width) / 2, (SIZE - 18 * unit) / 2
    dark = sum(colour) > 450
    ink = (20, 20, 24, 255) if dark else (255, 255, 255, 255)
    for position, char in enumerate(text):
        left = x0 + position * 13 * unit
        for segment in DIGITS[char]:
            a, b, c, d = SEGMENTS[segment]
            draw.rectangle((left + a * unit, y0 + b * unit, left + c * unit, y0 + d * unit), fill=ink)
    return image


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for index, colour in enumerate(colours(), start=1):
        target = OUT / f'x4lan_player{index}.dds'
        icon(index, tuple(colour)).save(target, format='DDS', pixel_format='DXT5')
        print('wrote', target)


if __name__ == '__main__':
    main()
