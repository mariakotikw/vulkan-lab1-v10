#!/usr/bin/env python3
"""Check committed RGB PNG material assets using only the Python stdlib."""

from __future__ import annotations

import binascii
import hashlib
from pathlib import Path
import struct
import zlib


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def paeth(left: int, above: int, upper_left: int) -> int:
    prediction = left + above - upper_left
    a, b, c = abs(prediction-left), abs(prediction-above), abs(prediction-upper_left)
    return left if a <= b and a <= c else above if b <= c else upper_left


def decode_png(path: Path) -> bytes:
    """CRC-check and unfilter the exact RGB8 format supplied to stb_image."""
    data = path.read_bytes()
    require(data[:8] == b"\x89PNG\r\n\x1a\n", f"{path.name}: invalid signature")
    position, compressed = 8, bytearray()
    dimensions = None
    ended = False
    while position < len(data):
        require(position+12 <= len(data), f"{path.name}: truncated chunk")
        length = struct.unpack_from(">I", data, position)[0]
        kind = data[position+4:position+8]
        payload = data[position+8:position+8+length]
        require(position+12+length <= len(data), f"{path.name}: truncated payload")
        crc = struct.unpack_from(">I", data, position+8+length)[0]
        require((binascii.crc32(kind+payload) & 0xFFFFFFFF) == crc,
                f"{path.name}: CRC failure in {kind!r}")
        if kind == b"IHDR":
            require(dimensions is None, f"{path.name}: duplicate header")
            width, height, bits, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            require((width, height) == (512, 512), f"{path.name}: expected 512x512")
            require((bits, color, compression, filtering, interlace) == (8, 2, 0, 0, 0),
                    f"{path.name}: expected non-interlaced RGB8")
            dimensions = width, height
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            require(length == 0, f"{path.name}: malformed end chunk")
            ended = True
            require(position+12 == len(data), f"{path.name}: data after end chunk")
            break
        position += 12+length
    require(dimensions is not None and ended and bool(compressed),
            f"{path.name}: missing PNG chunks")
    width, height = dimensions
    stride = width*3
    filtered = zlib.decompress(compressed)
    require(len(filtered) == (stride+1)*height, f"{path.name}: invalid pixel size")
    pixels = bytearray(stride*height)
    for row in range(height):
        mode = filtered[row*(stride+1)]
        require(mode in (0, 1, 2, 3, 4), f"{path.name}: invalid filter")
        start = row*stride
        values = filtered[row*(stride+1)+1:(row+1)*(stride+1)]
        for column, value in enumerate(values):
            index = start+column
            left = pixels[index-3] if column >= 3 else 0
            above = pixels[index-stride] if row else 0
            upper_left = pixels[index-stride-3] if row and column >= 3 else 0
            if mode == 1:
                value += left
            elif mode == 2:
                value += above
            elif mode == 3:
                value += (left+above)//2
            elif mode == 4:
                value += paeth(left, above, upper_left)
            pixels[index] = value & 255
    return bytes(pixels)


def measure(pixels: bytes) -> dict[str, float]:
    hist = [0]*256
    dark = bright = saturated = white = 0
    for index in range(0, len(pixels), 3):
        red, green, blue = pixels[index:index+3]
        high, low = max(red, green, blue), min(red, green, blue)
        dark += high <= 3
        bright += high > 32
        saturated += high-low >= 35
        white += low >= 245
        hist[(red+green+blue)//3] += 1
    count = len(pixels)//3
    cumulative = 0
    p05 = p95 = None
    for value, frequency in enumerate(hist):
        cumulative += frequency
        if p05 is None and cumulative >= count*.05:
            p05 = value
        if p95 is None and cumulative >= count*.95:
            p95 = value
    return {"mean": sum(pixels)/len(pixels), "dark": dark/count,
            "bright": bright/count, "saturated": saturated/count,
            "white": white/count, "range": float(p95-p05),
            "unique": float(sum(frequency > 0 for frequency in hist))}


def main() -> None:
    root = Path(__file__).resolve().parents[1]/"assets"/"textures"
    complete = ("cosmic_core", "nebula_mist", "holographic_prism")
    expected = {f"{name}_{kind}.png" for name in complete
                for kind in ("albedo", "specular", "emissive")}
    expected.add("prism_grid_albedo.png")
    require({path.name for path in root.glob("*.png")} == expected,
            "Expected exactly 10 PNGs, including albedo-only Prism Grid")
    decoded, stats = {}, {}
    for name in sorted(expected):
        decoded[name] = decode_png(root/name)
        stats[name] = measure(decoded[name])
        require(stats[name]["white"] < .001, f"{name}: too many white pixels")
        print(f"PASS {name}: RGB8 512x512, mean={stats[name]['mean']:.2f}, "
              f"near-black={stats[name]['dark']:.1%}, p95-p05={stats[name]['range']:.0f}")
    require(len({hashlib.sha256(data).digest() for data in decoded.values()}) == 10,
            "Material maps must all be different")
    for name in (*complete, "prism_grid"):
        values = stats[f"{name}_albedo.png"]
        require(values["range"] >= 20 and values["unique"] >= 70,
                f"{name}: albedo lacks tonal detail")
        require(values["saturated"] >= .12, f"{name}: albedo lacks color variation")
    for name in complete:
        specular = decoded[f"{name}_specular.png"]
        require(all(specular[i] == specular[i+1] == specular[i+2]
                    for i in range(0, len(specular), 3)), f"{name}: specular must be grayscale")
        require(stats[f"{name}_specular.png"]["range"] >= 18,
                f"{name}: specular map lacks contrast")
        values = stats[f"{name}_emissive.png"]
        require(values["dark"] >= .60, f"{name}: emission must remain local")
        require(values["bright"] >= .001, f"{name}: no visible emissive accents")
        require(max(decoded[f"{name}_emissive.png"]) >= 50,
                f"{name}: emission has no bright structure")
    require(stats["cosmic_core_albedo.png"]["mean"] < 30,
            "Cosmic Core should retain a dark base")
    require(stats["nebula_mist_albedo.png"]["mean"] > stats["cosmic_core_albedo.png"]["mean"]*1.5,
            "Nebula Mist should be visibly lighter than Cosmic Core")
    require(stats["cosmic_core_emissive.png"]["mean"] > stats["holographic_prism_emissive.png"]["mean"]*2,
            "Cosmic Core emission should be stronger than Holographic Prism")
    require(stats["holographic_prism_specular.png"]["mean"] > stats["cosmic_core_specular.png"]["mean"]*2,
            "Holographic Prism should have stronger specular response")
    # Emissive stars/filaments are painted into the same albedo image, rather
    # than unrelated random maps; bright cosmic emission should coincide with
    # stronger color/detail than its unlit background.
    albedo, emission = decoded["cosmic_core_albedo.png"], decoded["cosmic_core_emissive.png"]
    coincident = [sum(albedo[i:i+3])/3 for i in range(0, len(albedo), 3)
                  if max(emission[i:i+3]) > 80]
    require(bool(coincident) and sum(coincident)/len(coincident) > stats["cosmic_core_albedo.png"]["mean"]*1.15,
            "Cosmic emissive details should align with brighter albedo details")
    print("PASS 4 distinct presets / 10 original PNGs; contrast, locality and fallback asset layout")


if __name__ == "__main__":
    main()
