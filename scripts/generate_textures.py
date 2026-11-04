#!/usr/bin/env python3
"""Reproduce the original, deterministic Lab 3 material PNGs.

Requires Python 3.10+, NumPy and Pillow. The application needs only the committed
PNGs, not Python. Run from any directory: python scripts/generate_textures.py
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SIZE = 512
SEED = 31010
Y, X = np.mgrid[0:SIZE, 0:SIZE].astype(np.float64) / SIZE


def noise(rng: np.random.Generator, scale: float, octaves: int = 5) -> np.ndarray:
    """Periodic, band-limited fBm, without visible lattice or white-noise pixels."""
    fy = np.fft.fftfreq(SIZE)[:, None]
    fx = np.fft.rfftfreq(SIZE)[None, :]
    radius = np.sqrt(fx * fx + fy * fy) * SIZE
    field = np.zeros((SIZE, SIZE))
    weight = 0.0
    for level in range(octaves):
        frequency = scale * 2**level
        spectrum = np.fft.rfft2(rng.normal(size=(SIZE, SIZE)))
        band = np.exp(-0.5 * (radius / frequency) ** 2)
        band *= 1.0 - np.exp(-0.5 * (radius / (frequency * 0.35)) ** 2)
        octave = np.fft.irfft2(spectrum * band, s=(SIZE, SIZE))
        octave /= max(float(octave.std()), 1e-9)
        amplitude = 0.52**level
        field += amplitude * octave
        weight += amplitude
    return np.clip(0.5 + field / (weight * 5.0), 0.0, 1.0)


def rgb(value: np.ndarray) -> np.ndarray:
    return np.repeat(value[..., None], 3, axis=2)


def mask_image(mask: np.ndarray) -> Image.Image:
    return Image.fromarray(np.uint8(np.clip(mask, 0, 1) * 255))


def image_array(image: Image.Image) -> np.ndarray:
    return np.asarray(image, dtype=np.float64) / 255.0


def line_mask(paths: list[np.ndarray], width: int, blur: float = 0.0) -> np.ndarray:
    canvas = Image.new("L", (SIZE, SIZE))
    draw = ImageDraw.Draw(canvas)
    for points in paths:
        draw.line([(float(x * SIZE), float(y * SIZE)) for x, y in points],
                  fill=255, width=width, joint="curve")
    if blur:
        canvas = canvas.filter(ImageFilter.GaussianBlur(blur))
    return image_array(canvas)


def stars(rng: np.random.Generator, cloud: np.ndarray,
          count: int = 195) -> tuple[np.ndarray, np.ndarray]:
    """Fine dust plus a handful of deliberately placed, softly radiating stars."""
    small = Image.new("L", (SIZE, SIZE))
    draw = ImageDraw.Draw(small)
    for _ in range(count):
        px, py = rng.integers(3, SIZE - 3, size=2)
        if rng.random() > 0.24 + 0.76 * cloud[py, px]:
            continue
        draw.point((int(px), int(py)), fill=int(rng.uniform(70, 220)))
    dust = image_array(small)
    bright = Image.new("L", (SIZE, SIZE))
    draw = ImageDraw.Draw(bright)
    for px, py, radius in [(82, 116, 2), (351, 88, 2), (216, 331, 1),
                           (421, 374, 2), (127, 417, 1), (302, 228, 1),
                           (391, 151, 1), (50, 276, 1)]:
        draw.ellipse((px-radius, py-radius, px+radius, py+radius), fill=210)
        draw.line((px-3*radius, py, px+3*radius, py), fill=110, width=1)
        draw.line((px, py-3*radius, px, py+3*radius), fill=95, width=1)
    core = image_array(bright)
    halo = image_array(bright.filter(ImageFilter.GaussianBlur(3.2)))
    return dust + core * 0.7 + halo * 2.4, core + halo * 1.5


def cosmic(rng: np.random.Generator, mist: bool = False) -> dict[str, np.ndarray]:
    broad = noise(rng, 2.3)
    detail = noise(rng, 8.0)
    micro = noise(rng, 26.0, 3)
    warp = noise(rng, 4.8)
    cloud = np.clip((broad - 0.25) * 2.1, 0, 1) ** 1.6
    smoke = np.clip((detail - 0.30) * 2.1, 0, 1)
    ribbon = np.exp(-((Y - 0.65 + X * 0.36 + (warp - .5) * .6) / .22) ** 2)
    cloud *= 0.30 + 0.70 * ribbon
    purple = np.clip(.5 + (warp - .5) * 3 + .3*np.sin(8*X - 5*Y), 0, 1)
    blue_tint = np.array([.055, .13, .34])
    purple_tint = np.array([.24, .055, .32])
    nebula_color = (1-purple[..., None])*blue_tint + purple[..., None]*purple_tint
    albedo = np.zeros((SIZE, SIZE, 3)) + [.008, .012, .035]
    albedo += nebula_color * (cloud * (.45 + smoke) * (1.8 if mist else 1.30))[..., None]
    albedo += rgb((micro-.5) * .012)
    t = np.linspace(-.12, 1.12, 780)
    main_y = (.84 - .68*t + .065*np.sin(11*t+.4) + .022*np.sin(33*t)
              + .006*np.sin(89*t+.3) + .0025*np.sin(187*t))
    main = np.column_stack((t, main_y))
    q = np.linspace(0, 1, 450)
    branches = [
        np.column_stack((.40-.39*q+.018*np.sin(q*19)+.003*np.sin(q*111),
                         .51-.42*q+.035*np.sin(q*10))),
        np.column_stack((.63+.35*q+.02*np.sin(q*17)+.004*np.sin(q*95),
                         .46+.45*q+.035*np.sin(q*12))),
        np.column_stack((.24+.32*q+.018*np.sin(q*23)+.003*np.sin(q*128),
                         .64+.35*q+.045*np.sin(q*9))),
    ]
    paths = [main] + branches
    fine = line_mask(paths, 1, .4)
    body = line_mask(paths, 3, 1.8)
    halo = line_mask(paths, 5, 9)
    cyan_mix = .5 + .5*np.sin(7*X + 9*Y + warp*3)
    energy_tint = ((1-cyan_mix[..., None])*np.array([.30, .055, .95])
                   + cyan_mix[..., None]*np.array([.025, .72, 1.0]))
    energy = fine * .65 + body * .44 + halo * .43
    star_field, star_emission = stars(rng, cloud, 280 if mist else 195)
    albedo += energy_tint * (energy * (.19 if mist else .30))[..., None]
    albedo += star_field[..., None] * np.array([.48, .60, .75])
    # Black outside selected filaments/cloud knots: emission remains local.
    cloud_knots = np.clip((cloud*smoke - .42) * 3.2, 0, 1) ** 2
    emission = energy_tint * (energy * (.48 if mist else .95))[..., None]
    emission += nebula_color * cloud_knots[..., None] * (1.1 if mist else .7)
    emission += star_emission[..., None] * np.array([.28, .52, .78])
    specular = np.clip(.035 + .24*cloud*smoke + .16*body + .05*micro, .02, .5)
    if mist:
        albedo += np.array([.024, .035, .075]) + cloud[..., None]*[.04, .05, .10]
        albedo *= 1.12
    return {"albedo": albedo, "specular": rgb(specular), "emissive": emission}


def periodic_line(phase: np.ndarray, width: float) -> np.ndarray:
    distance = np.abs((phase + .5) % 1.0 - .5)
    return np.clip(1.0 - distance / width, 0, 1)


def holographic(rng: np.random.Generator, grid_only: bool = False) -> dict[str, np.ndarray]:
    soft = noise(rng, 3.2)
    grain = noise(rng, 23, 3)
    diagonal = X*.82 + Y*.57
    phase = 2*np.pi*(diagonal*2.0 + .14*np.sin(2*np.pi*(X-Y)) + (soft-.5)*.38)
    # A controlled cyan / pink / violet palette; no white band in the spectrum.
    rainbow = np.stack((.38+.30*np.cos(phase+.6),
                        .34+.28*np.cos(phase-1.55),
                        .55+.22*np.cos(phase+2.65)), axis=-1)
    bands = .91 + .06*np.sin(2*np.pi*(diagonal*49+(soft-.5)*.8))
    interference = .028*np.sin(2*np.pi*(X*91-Y*39+.13*np.sin(Y*11)))
    albedo = rainbow * bands[..., None] + interference[..., None]
    albedo += (grain-.5)[..., None]*.035
    # Triangle facets and a fine isometric lattice give readable large/small scales.
    cellx, celly = (X+Y*.36)*7, (Y-X*.18)*7
    facet = ((cellx % 1) > (celly % 1)).astype(float)
    albedo *= (.87+.13*facet)[..., None]
    lattice = np.maximum(periodic_line(cellx, .018), periodic_line(celly, .018))
    diagonal_lines = periodic_line((X*.91-Y*.49)*22, .025)
    albedo += lattice[..., None] * [.10, .13, .17]
    albedo += diagonal_lines[..., None] * [.025, .055, .07]
    circuitry = Image.new("L", (SIZE, SIZE))
    accents = Image.new("L", (SIZE, SIZE))
    draw, nodes = ImageDraw.Draw(circuitry), ImageDraw.Draw(accents)
    # Sparse traces, right-angle elbows, outlined microchips and tiny data bars.
    for k in range(15):
        px = int(rng.integers(24, SIZE-100))
        py = int(rng.integers(24, SIZE-60))
        length = int(rng.integers(22, 78))
        shift = int(rng.choice([-15, 15, 22]))
        draw.line([(px, py), (px+length//2, py),
                   (px+length//2+abs(shift), py+shift),
                   (px+length, py+shift)], fill=150, width=1)
        nodes.rectangle((px-2, py-2, px+2, py+2), outline=225, width=1)
        nodes.rectangle((px+length-2, py+shift-2, px+length+2, py+shift+2), fill=180)
        if k % 3 == 0:
            draw.rectangle((px+4, py+8, px+19, py+21), outline=110, width=1)
            for bar in range(4):
                draw.line((px+26+bar*3, py+10, px+26+bar*3, py+15+(bar%2)*4),
                          fill=145, width=1)
    trace, node = image_array(circuitry), image_array(accents)
    trace_halo = image_array(circuitry.filter(ImageFilter.GaussianBlur(1.5)))
    neon_tint = np.stack((.16+.44*(.5+.5*np.sin(phase)),
                         .38+.27*(.5+.5*np.cos(phase)),
                         .70+np.zeros_like(phase)), axis=-1)
    albedo += neon_tint*(trace*.36+node*.32)[..., None]
    if grid_only:
        albedo *= .68
        thicker = np.maximum(periodic_line(cellx, .045), periodic_line(celly, .045))
        albedo += neon_tint*thicker[..., None]*.38
        return {"albedo": albedo}
    specular = .22+.36*(.5+.5*np.sin(phase+.8))**2
    specular += .12*facet + .12*lattice + .08*trace
    emission = neon_tint*(trace*.23 + node*.39 + trace_halo*.18)[..., None]
    # Only selected lattice segments glow; the base hologram stays lit by lights.
    selected = (np.sin(2*np.pi*(X*2+Y*3)) > .78).astype(float)
    emission += neon_tint*(lattice*selected*.13)[..., None]
    return {"albedo": albedo, "specular": rgb(np.clip(specular, 0, .87)),
            "emissive": emission}


def save_map(path: Path, array: np.ndarray) -> None:
    pixels = np.uint8(np.rint(np.clip(array, 0, 1)*255))
    Image.fromarray(pixels).save(path, optimize=True)


def contact_sheet(materials: dict[str, dict[str, np.ndarray]], destination: Path) -> None:
    """Optional review artifact outside the application asset directory."""
    thumb, title_height = 256, 30
    result = Image.new("RGB", (thumb*3, (thumb+title_height)*len(materials)), (12, 15, 24))
    draw = ImageDraw.Draw(result)
    for row, (name, maps) in enumerate(materials.items()):
        for column, kind in enumerate(("albedo", "specular", "emissive")):
            px, py = column*thumb, row*(thumb+title_height)
            draw.text((px+8, py+8), f"{name} / {kind}", fill=(211, 221, 239))
            if kind not in maps:
                draw.text((px+18, py+100), "Black fallback (map intentionally absent)",
                          fill=(125, 142, 164))
                continue
            pixels = np.uint8(np.rint(np.clip(maps[kind], 0, 1)*255))
            image = Image.fromarray(pixels).resize((thumb, thumb), Image.Resampling.LANCZOS)
            result.paste(image, (px, py+title_height))
    destination.parent.mkdir(parents=True, exist_ok=True)
    result.save(destination)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1]/"assets"/"textures")
    parser.add_argument("--contact-sheet", type=Path,
                        help="Optional contact sheet; excluded from application assets")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    # Separate streams keep other presets stable when editing one generator.
    materials = {
        "cosmic_core": cosmic(np.random.default_rng(SEED)),
        "nebula_mist": cosmic(np.random.default_rng(SEED+1), mist=True),
        "holographic_prism": holographic(np.random.default_rng(SEED+2)),
        "prism_grid": holographic(np.random.default_rng(SEED+3), grid_only=True),
    }
    for name, maps in materials.items():
        for kind, array in maps.items():
            path = args.output/f"{name}_{kind}.png"
            save_map(path, array)
            print(f"{path.name}: {SIZE}x{SIZE} RGB")
    if args.contact_sheet:
        contact_sheet(materials, args.contact_sheet)


if __name__ == "__main__":
    main()
