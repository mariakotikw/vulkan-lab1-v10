#!/usr/bin/env python3
"""Measure actual Lab 3 desktop captures; write diagnostics before failing.

Requires NumPy and Pillow. Captures must use the same 1440x900 outer-window
geometry, paused object animation and the UI scenarios documented by their
filenames. This checks rendered pixels; it does not simulate shader output.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


FILES = (
    "default.png", "hologram-a.png", "hologram-b.png",
    "static-a.png", "static-b.png", "dark-fallback.png",
    "dark-emissive.png", "dark-emissive-off.png",
    "specular-on.png", "specular-off.png", "albedo-off.png",
)
BACKGROUND = np.array([31, 37, 48], dtype=np.int16)
DIFFERENCE = 2
MIN_EFFECT_PIXELS = 200
MIN_EMISSIVE_PIXELS = 100
MAX_STATIC_PIXELS = 10


def changed(first: np.ndarray, second: np.ndarray) -> dict:
    delta = np.abs(first.astype(np.int16) - second.astype(np.int16))
    per_pixel = delta.max(axis=2)
    return {
        "changed_pixels": int(np.count_nonzero(per_pixel > DIFFERENCE)),
        "total_pixels": int(per_pixel.size),
        "max_channel_delta": int(delta.max()),
        "mean_channel_delta": float(delta.mean()),
    }


def foreground(image: np.ndarray) -> np.ndarray:
    pixels = image.astype(np.int16)
    # Clear color appears in the sRGB swapchain near RGB 31/37/48. A small
    # tolerance excludes its quantization without hiding dark model pixels.
    background = np.max(np.abs(pixels - BACKGROUND), axis=2) <= 3
    return ~background


def emission_metrics(image: np.ndarray) -> dict:
    pixels = image.astype(np.int16)
    maximum = pixels.max(axis=2)
    chroma = maximum - pixels.min(axis=2)
    mask = foreground(image)
    return {
        "visible_pixels": int(np.count_nonzero(mask & (maximum > 45))),
        "colorful_pixels": int(np.count_nonzero(mask & (maximum > 45) & (chroma > 16))),
        "foreground_pixels": int(np.count_nonzero(mask)),
        "max_channel": int(maximum[mask].max()) if np.any(mask) else 0,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--image-dir", type=Path,
        default=Path(__file__).resolve().parents[1] / "docs/lab3/testing/textures-ui",
    )
    parser.add_argument("--output", type=Path,
                        help="Defaults to IMAGE_DIR/image-checks.json")
    args = parser.parse_args()
    destination = args.output or args.image_dir / "image-checks.json"
    report = {
        "passed": False,
        "image_directory": str(args.image_dir.resolve()),
        "background_rgb": BACKGROUND.tolist(),
        "difference_threshold": DIFFERENCE,
        "checks": [],
    }

    def check(name: str, passed: bool, metrics: dict, limits: str) -> None:
        report["checks"].append({"name": name, "passed": bool(passed),
                                 "metrics": metrics, "limits": limits})

    try:
        images = {}
        dimensions = {}
        expected_shape = None
        for filename in FILES:
            with Image.open(args.image_dir / filename) as source:
                pixels = np.asarray(source.convert("RGB"))
            height, width = pixels.shape[:2]
            if width < 1000 or height < 600:
                raise ValueError(f"Capture too small: {filename} ({width}x{height})")
            if expected_shape is None:
                expected_shape = pixels.shape
            if pixels.shape != expected_shape:
                raise ValueError(f"Capture dimensions differ: {filename}")
            # Remove title/border and the right-hand ImGui panel. Do not include
            # changing FPS text or the Windows titlebar in motion comparisons.
            images[filename] = pixels[50:-16, 16:width - 392]
            dimensions[filename] = {"width": width, "height": height}
        report["images"] = dimensions
        report["crop"] = {"top": 50, "bottom_margin": 16,
                          "left": 16, "right_margin": 392}

        def half(filename: str, side: str) -> np.ndarray:
            image = images[filename]
            midpoint = image.shape[1] // 2
            return image[:, :midpoint] if side == "left" else image[:, midpoint:]

        dynamic_right = changed(half("hologram-a.png", "right"), half("hologram-b.png", "right"))
        dynamic_left = changed(half("hologram-a.png", "left"), half("hologram-b.png", "left"))
        check("paused_geometry_hologram_moves", dynamic_right["changed_pixels"] >= MIN_EFFECT_PIXELS,
              dynamic_right, f"At least {MIN_EFFECT_PIXELS} right pixels differ by >{DIFFERENCE}")
        check("hologram_does_not_change_left_object", dynamic_left["changed_pixels"] < MAX_STATIC_PIXELS,
              dynamic_left, f"Fewer than {MAX_STATIC_PIXELS} left pixels differ by >{DIFFERENCE}")

        static = changed(images["static-a.png"], images["static-b.png"])
        check("disabled_effect_is_static", static["changed_pixels"] < MAX_STATIC_PIXELS,
              static, f"Fewer than {MAX_STATIC_PIXELS} scene pixels differ by >{DIFFERENCE}")

        fallback_left = emission_metrics(half("dark-fallback.png", "left"))
        fallback_right = emission_metrics(half("dark-fallback.png", "right"))
        check("cosmic_emissive_survives_without_lights", fallback_left["colorful_pixels"] > MIN_EMISSIVE_PIXELS,
              fallback_left, f"More than {MIN_EMISSIVE_PIXELS} colorful non-background pixels")
        check("missing_emissive_black_fallback_is_dark", fallback_right["visible_pixels"] < MAX_STATIC_PIXELS,
              fallback_right, f"Fewer than {MAX_STATIC_PIXELS} non-background pixels with a channel >45")

        emissive_right = emission_metrics(half("dark-emissive.png", "right"))
        emissive_off_right = emission_metrics(half("dark-emissive-off.png", "right"))
        check("hologram_emissive_survives_without_lights", emissive_right["colorful_pixels"] > MIN_EMISSIVE_PIXELS,
              emissive_right, f"More than {MIN_EMISSIVE_PIXELS} colorful non-background pixels")
        check("disabled_hologram_emissive_is_dark", emissive_off_right["visible_pixels"] < MAX_STATIC_PIXELS,
              emissive_off_right, f"Fewer than {MAX_STATIC_PIXELS} non-background pixels with a channel >45")
        emissive_toggle = changed(half("dark-emissive.png", "right"), half("dark-emissive-off.png", "right"))
        check("emissive_toggle_changes_rendered_pixels", emissive_toggle["changed_pixels"] >= MIN_EFFECT_PIXELS,
              emissive_toggle, f"At least {MIN_EFFECT_PIXELS} right pixels differ by >{DIFFERENCE}")
        for previous, current in (("dark-fallback.png", "dark-emissive.png"),
                                  ("dark-emissive.png", "dark-emissive-off.png")):
            stable_left = changed(half(previous, "left"), half(current, "left"))
            check(f"left_independent_{previous}_to_{current}", stable_left["changed_pixels"] < MAX_STATIC_PIXELS,
                  stable_left, f"Fewer than {MAX_STATIC_PIXELS} left pixels differ by >{DIFFERENCE}")

        specular_on = half("specular-on.png", "right").astype(np.int16)
        specular_off = half("specular-off.png", "right").astype(np.int16)
        signed_delta = specular_on - specular_off
        positive = np.max(signed_delta, axis=2) > DIFFERENCE
        negative = np.min(signed_delta, axis=2) < -DIFFERENCE
        untouched = np.max(np.abs(signed_delta), axis=2) <= DIFFERENCE
        lit_surface = foreground(specular_off) & (specular_off.max(axis=2) > 20)
        specular = {
            **changed(specular_on, specular_off),
            "brighter_pixels": int(np.count_nonzero(positive)),
            "darker_pixels": int(np.count_nonzero(negative)),
            "unchanged_lit_surface_pixels": int(np.count_nonzero(untouched & lit_surface)),
        }
        check("specular_map_adds_highlights", specular["brighter_pixels"] >= MIN_EFFECT_PIXELS,
              specular, f"At least {MIN_EFFECT_PIXELS} right pixels gain >{DIFFERENCE} in a channel")
        check("specular_preserves_unaffected_diffuse_pixels",
              specular["darker_pixels"] < MAX_STATIC_PIXELS and specular["unchanged_lit_surface_pixels"] >= MIN_EFFECT_PIXELS,
              specular, f"Fewer than {MAX_STATIC_PIXELS} darker pixels; at least {MIN_EFFECT_PIXELS} unchanged lit surface pixels")
        specular_left = changed(half("specular-on.png", "left"), half("specular-off.png", "left"))
        check("specular_toggle_does_not_change_left_object", specular_left["changed_pixels"] < MAX_STATIC_PIXELS,
              specular_left, f"Fewer than {MAX_STATIC_PIXELS} left pixels differ by >{DIFFERENCE}")

        albedo_right = changed(half("specular-off.png", "right"), half("albedo-off.png", "right"))
        albedo_left = changed(half("specular-off.png", "left"), half("albedo-off.png", "left"))
        check("albedo_toggle_changes_right_surface", albedo_right["changed_pixels"] >= MIN_EFFECT_PIXELS,
              albedo_right, f"At least {MIN_EFFECT_PIXELS} right pixels differ by >{DIFFERENCE}")
        check("albedo_toggle_does_not_change_left_object", albedo_left["changed_pixels"] < MAX_STATIC_PIXELS,
              albedo_left, f"Fewer than {MAX_STATIC_PIXELS} left pixels differ by >{DIFFERENCE}")

        for side in ("left", "right"):
            image = half("default.png", side)
            mask = foreground(image) & (image.max(axis=2) > 20)
            visible_count = int(np.count_nonzero(mask))
            near_white = int(np.count_nonzero(mask & (image.min(axis=2) >= 240)))
            proportion = near_white / max(visible_count, 1)
            check(f"default_{side}_surface_is_not_washed_out", visible_count >= 1000 and proportion < 0.10,
                  {"visible_surface_pixels": visible_count, "near_white_pixels": near_white,
                   "near_white_fraction": proportion},
                  "At least 1000 visible surface pixels; less than 10% have every RGB channel >=240")

        report["passed"] = all(item["passed"] for item in report["checks"])
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    for item in report["checks"]:
        print(f"{'PASS' if item['passed'] else 'FAIL'}: {item['name']}: {json.dumps(item['metrics'])}")
    if "error" in report:
        print(f"FAIL: {report['error']}")
    print(f"Results: {destination}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
