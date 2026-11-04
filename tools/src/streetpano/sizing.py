"""Size estimates used to plan memory, disk space and output dimensions."""

import math

BYTES_PER_GB = 1_000_000_000


def _require_positive(**values: float) -> None:
    for name, value in values.items():
        if value <= 0:
            raise ValueError(f"{name} must be positive, got {value}")


def image_bytes(pixels: float, bit_depth: int = 8, channels: int = 3) -> float:
    """Return the uncompressed size of an image in bytes."""
    _require_positive(pixels=pixels, channels=channels)
    if bit_depth not in (8, 16):
        raise ValueError(f"bit_depth must be 8 or 16, got {bit_depth}")
    return pixels * channels * bit_depth / 8


def panorama_width_px(length_m: float, gsd_m: float) -> int:
    """Return the panorama width for a street length and pixel size on the facade."""
    _require_positive(length_m=length_m, gsd_m=gsd_m)
    return round(length_m / gsd_m)


def photo_count(length_m: float, spacing_m: float) -> int:
    """Return the number of photos needed to cover a street length."""
    _require_positive(length_m=length_m, spacing_m=spacing_m)
    return math.ceil(length_m / spacing_m)


def slope_extra_height_px(length_m: float, slope: float, gsd_m: float) -> int:
    """Return the extra panorama height caused by a street's overall slope.

    ``slope`` is a fraction, so 0.05 is a 5% slope. Uphill and downhill streets
    need the same extra height.
    """
    _require_positive(length_m=length_m, gsd_m=gsd_m)
    return round(length_m * abs(slope) / gsd_m)


def pyramid_levels(width_px: int, height_px: int, tile_px: int = 512) -> int:
    """Return how many zoom levels are needed until the image fits in one tile."""
    _require_positive(width_px=width_px, height_px=height_px, tile_px=tile_px)
    longest_side = max(width_px, height_px)
    return max(0, math.ceil(math.log2(longest_side / tile_px))) + 1


def max_visible_tiles(viewport_w: int, viewport_h: int, tile_px: int = 512) -> int:
    """Return the most tiles a viewport can touch, at any position or zoom level.

    Each zoom level shows pixels at no more than about screen resolution, so this
    limit does not depend on the panorama's size.
    """
    _require_positive(viewport_w=viewport_w, viewport_h=viewport_h, tile_px=tile_px)
    columns = math.ceil(viewport_w / tile_px) + 1
    rows = math.ceil(viewport_h / tile_px) + 1
    return columns * rows
