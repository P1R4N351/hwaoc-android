#!/usr/bin/env python3
"""Draw the AoC Native launcher emblem: an 8-point Calamity star-gear crest inside the 48-tick slate ring.

    python3 make_icon_hwaoc.py [--out hwaoc-native-1024.png] [--size 1024]
Deterministic: pure Pillow vector geometry, no fonts, no randomness, transparent outside the disc.
"""
import argparse
import math
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
SLATE, DEEP = (30, 14, 18, 255), (15, 7, 10, 255)
EMBER, EMBER_DIM, GOLD = (242, 82, 68, 255), (146, 38, 34, 255), (232, 186, 86, 255)
SS = 4


def _polar(center, radius, degrees):
    """Convert clockwise degree angle (0 at top) to pixel coordinates."""
    angle = math.radians(degrees - 90.0)
    return center + radius * math.cos(angle), center + radius * math.sin(angle)


def _star_points(center, outer_r, inner_r, points, phase_deg):
    """Build alternating outer/inner vertices for a regular radial crest."""
    verts = []
    step = 180.0 / points
    for idx in range(points * 2):
        r = outer_r if idx % 2 == 0 else inner_r
        verts.append(_polar(center, r, phase_deg + idx * step))
    return verts


def _draw_crest(draw, c, u):
    """Draw outer tick ring, cardinal arcs, octagonal Calamity star, and crossed blades."""
    draw.ellipse((c - 470 * u, c - 470 * u, c + 470 * u, c + 470 * u), outline=EMBER, width=round(12 * u))
    draw.ellipse((c - 398 * u, c - 398 * u, c + 398 * u, c + 398 * u), outline=GOLD, width=round(5 * u))
    for i in range(48):
        major = i % 6 == 0
        r_in = (404 if major else 420) * u
        draw.line([_polar(c, r_in, i * 7.5), _polar(c, 452 * u, i * 7.5)],
                  fill=GOLD if major else EMBER_DIM, width=round((9 if major else 5) * u))
    outer_star = _star_points(c, 310 * u, 168 * u, 8, 22.5)
    draw.line(outer_star + [outer_star[0], outer_star[1]], fill=EMBER, width=round(16 * u), joint='curve')
    inner_star = _star_points(c, 245 * u, 118 * u, 4, 0.0)
    draw.line(inner_star + [inner_star[0], inner_star[1]], fill=GOLD, width=round(14 * u), joint='curve')
    draw.ellipse((c - 92 * u, c - 92 * u, c + 92 * u, c + 92 * u), outline=EMBER, width=round(14 * u))
    draw.ellipse((c - 40 * u, c - 40 * u, c + 40 * u, c + 40 * u), fill=GOLD)
    for deg in (0, 90, 180, 270):
        draw.line([_polar(c, 110 * u, deg), _polar(c, 344 * u, deg)], fill=EMBER, width=round(10 * u))


def emblem(size):
    """Render the complete antialiased RGBA emblem at `size x size`."""
    n = size * SS
    c, u = n / 2.0, n / 1024.0
    base = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    shade = Image.linear_gradient('L').resize((n, n)).point(lambda v: v * 0.55)
    disc = Image.composite(Image.new('RGBA', (n, n), DEEP), Image.new('RGBA', (n, n), SLATE), shade)
    mask = Image.new('L', (n, n), 0)
    ImageDraw.Draw(mask).ellipse((c - 486 * u, c - 486 * u, c + 486 * u, c + 486 * u), fill=255)
    base.paste(disc, (0, 0), mask)

    lines = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    _draw_crest(ImageDraw.Draw(lines), c, u)
    glow = lines.filter(ImageFilter.GaussianBlur(22 * u))
    glow.putalpha(glow.getchannel('A').point(lambda v: v * 0.9))
    glow = Image.composite(glow, Image.new('RGBA', (n, n), (0, 0, 0, 0)), mask)
    out = Image.alpha_composite(Image.alpha_composite(base, glow), lines)
    edge = Image.new('L', (n, n), 0)
    ImageDraw.Draw(edge).ellipse((c - 492 * u, c - 492 * u, c + 492 * u, c + 492 * u), fill=255)
    out.putalpha(ImageChops.multiply(out.getchannel('A'), edge))
    return out.resize((size, size), Image.Resampling.LANCZOS)


def main(argv=None):
    """CLI entry point for writing hwaoc-native-1024.png."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=HERE / 'hwaoc-native-1024.png')
    parser.add_argument('--size', type=int, default=1024)
    args = parser.parse_args(argv)
    emblem(args.size).save(args.out, format='PNG', optimize=True)
    print(f'wrote {args.out}')


if __name__ == '__main__':
    main()
