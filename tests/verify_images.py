"""Check actual rendered changes after verify_controls.ps1; requires numpy and Pillow."""
from pathlib import Path
import json

import numpy as np
from PIL import Image


def main():
    root = Path(__file__).resolve().parent.parent / '.tools' / 'logs' / 'extras'
    snapshots = json.loads((root / 'snapshots.json').read_text(encoding='utf-8-sig'))
    # Exclude the title bar, borders, and ImGui controls from pixel comparisons.
    right = int(snapshots['final']['rects']['perspective'][0] - 12)

    def figure(name):
        rgb = np.asarray(Image.open(root / name).convert('RGB'))[50:-16, 16:right]
        return rgb, np.max(rgb, axis=2) > 20

    base, base_mask = figure('perspective.png')
    tinted, tint_mask = figure('tinted.png')
    _, scale_mask = figure('scaled.png')
    base_mean = base[base_mask].mean(axis=0)
    tint_mean = tinted[tint_mask].mean(axis=0)
    ratio = float(scale_mask.sum() / base_mask.sum())
    unique = int(len(np.unique(base[base_mask], axis=0)))
    checks = {
        'rendered_scale_area_ratio': ratio,
        'rendered_scale_changes_size': 0.15 < ratio < 0.5,
        'baseline_mean_rgb': base_mean.tolist(),
        'tinted_mean_rgb': tint_mean.tolist(),
        'rendered_tint_changes_color': bool(
            tint_mean[0] < base_mean[0] * 0.7 and tint_mean[2] < base_mean[2] * 0.8),
        'distinct_surface_rgb_values': unique,
        'colors_interpolate_on_surface': unique > 1000,
    }
    (root / 'image-checks.json').write_text(json.dumps(checks, indent=2), encoding='utf-8')
    print(json.dumps(checks, indent=2))
    if not all(checks[key] for key in (
            'rendered_scale_changes_size', 'rendered_tint_changes_color', 'colors_interpolate_on_surface')):
        raise SystemExit('Rendered image checks failed')


if __name__ == '__main__':
    main()
