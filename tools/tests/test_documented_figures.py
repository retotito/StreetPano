"""Check the figures quoted in the concept documentation.

If a figure changes, update the matching page in ``docs/concept``.
"""

import pytest

from streetpano.geometry import (
    focal_length_px,
    ground_sample_distance_m,
    hidden_facade_height_m,
    parallax_shift_px,
)
from streetpano.sizing import (
    BYTES_PER_GB,
    image_bytes,
    panorama_width_px,
    slope_extra_height_px,
)

FOCAL_PX = focal_length_px(24, 36, 6000)
GSD_M = ground_sample_distance_m(15, FOCAL_PX)


@pytest.mark.parametrize(
    ("spacing_m", "object_m", "expected_px"),
    [
        (3, 13.5, 89),
        (1, 13.5, 30),
        (3, 8, 700),
        (1, 8, 233),
        (3, 5, 1600),
        (1, 5, 533),
    ],
)
def test_parallax_table(spacing_m: float, object_m: float, expected_px: int) -> None:
    shift = parallax_shift_px(FOCAL_PX, spacing_m, object_m, 15)
    assert shift == pytest.approx(expected_px, abs=0.5)


def test_pixel_size_on_facade() -> None:
    assert GSD_M == pytest.approx(0.00375)


def test_parked_car_hides_the_bottom_of_the_facade() -> None:
    hidden = hidden_facade_height_m(1.6, 1.5, 5, 7, 15)
    assert hidden == pytest.approx(1.39, abs=0.01)


@pytest.mark.parametrize(
    ("length_m", "bit_depth", "expected_gb"),
    [(500, 8, 1.6), (500, 16, 3.2), (5000, 8, 16), (5000, 16, 32)],
)
def test_panorama_size_table(length_m: int, bit_depth: int, expected_gb: float) -> None:
    pixels = panorama_width_px(length_m, GSD_M) * 4000
    size = image_bytes(pixels, bit_depth=bit_depth) / BYTES_PER_GB
    assert size == pytest.approx(expected_gb, abs=0.05)


def test_developed_photo_size() -> None:
    assert image_bytes(24_000_000, bit_depth=16) == 144_000_000


def test_extra_height_for_sloped_street() -> None:
    assert slope_extra_height_px(500, 0.05, GSD_M) == pytest.approx(6667, abs=1)
