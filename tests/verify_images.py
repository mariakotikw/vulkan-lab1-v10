from pathlib import Path
import json

import numpy as np
from PIL import Image


def main():
    root = Path(__file__).resolve().parent.parent / 'docs' / 'testing' / 'ui'

    def figure(name):
        image = np.asarray(Image.open(root / name).convert('RGB'))
        rgb = image[50:-16, 16:image.shape[1] - 392]
        return rgb, (np.max(rgb, axis=2) > 80) & ((np.max(rgb, axis=2).astype(int) - np.min(rgb, axis=2)) > 30)

    def spans(mask):
        columns = mask.any(axis=0)
        boundaries = np.flatnonzero(np.diff(np.r_[False, columns, False]))
        return [(a, b) for a, b in zip(boundaries[::2], boundaries[1::2]) if b - a > 40]

    base, base_mask = figure('perspective.png')
    runs = spans(base_mask)
    if len(runs) != 2:
        raise SystemExit(f'Expected two separated visible objects, got {runs}')
    split = int((runs[0][1] + runs[1][0]) // 2)
    tinted, tint_mask = figure('tinted.png')
    second_tinted, second_tint_mask = figure('object2-tinted.png')
    scaled, scale_mask = figure('scaled.png')
    left, right = np.s_[:, :split], np.s_[:, split:]
    base_left_mean = base[left][base_mask[left]].mean(axis=0)
    tint_left_mean = tinted[left][tint_mask[left]].mean(axis=0)
    base_right_mean = base[right][base_mask[right]].mean(axis=0)
    tint_right_mean = second_tinted[right][second_tint_mask[right]].mean(axis=0)
    ratio = float(scale_mask[left].sum() / base_mask[left].sum())
    unique = [int(len(np.unique(base[region][base_mask[region]], axis=0))) for region in (left, right)]
    checks = {
        'two_visible_separated_objects': len(runs) == 2,
        'visible_object_column_ranges': [[int(a), int(b)] for a, b in runs],
        'rendered_scale_area_ratio_object1': ratio,
        'rendered_scale_changes_object1_size': 0.15 < ratio < 0.5,
        'object1_scale_leaves_object2_pixels_unchanged': bool(np.array_equal(base[right], scaled[right])),
        'object1_tint_changes_its_pixels': bool(np.linalg.norm(base_left_mean - tint_left_mean) > 25),
        'object1_tint_leaves_object2_pixels_unchanged': bool(np.array_equal(base[right], tinted[right])),
        'object2_tint_changes_its_pixels': bool(np.linalg.norm(base_right_mean - tint_right_mean) > 25),
        'object2_tint_leaves_object1_pixels_unchanged': bool(np.array_equal(base[left], second_tinted[left])),
        'different_object_mean_rgb': bool(np.linalg.norm(base_left_mean - base_right_mean) > 30),
        'distinct_surface_rgb_values_per_object': unique,
        'lighting_varies_across_visible_surfaces': all(count > 12 for count in unique),
    }
    for name in ('orthographic.png', 'resized-perspective.png', 'resized-orthographic.png'):
        _, mask = figure(name)
        ranges = spans(mask)
        checks[f'two_complete_objects_in_{name}'] = bool(
            len(ranges) == 2 and ranges[0][0] > 0 and ranges[-1][1] < mask.shape[1]
            and not mask[0].any() and not mask[-1].any())
    lighting_root = root.parent / 'lighting-ui'
    dark = np.asarray(Image.open(lighting_root / 'all-lights-off.png').convert('RGB')).astype(int)
    lit = np.asarray(Image.open(lighting_root / 'directional-only.png').convert('RGB')).astype(int)
    width = dark.shape[1] - 392
    changed_pixels = int((np.abs(dark[50:-16, 16:width] - lit[50:-16, 16:width]).max(axis=2) > 20).sum())
    lighting_checks = {
        'directional_light_changes_rendered_scene': changed_pixels > 15000,
        'changed_scene_pixels': changed_pixels,
    }
    (lighting_root / 'image-checks.json').write_text(json.dumps(lighting_checks, indent=2), encoding='utf-8')
    checks.update(lighting_checks)
    (root / 'image-checks.json').write_text(json.dumps(checks, indent=2), encoding='utf-8')
    print(json.dumps(checks, indent=2))
    if not all(value for value in checks.values() if isinstance(value, bool)):
        raise SystemExit('Rendered image checks failed')


if __name__ == '__main__':
    main()
