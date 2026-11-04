import pytest
from hypothesis import given
from hypothesis import strategies as st

from streetpano.sizing import (
    image_bytes,
    max_visible_tiles,
    panorama_width_px,
    photo_count,
    pyramid_levels,
    slope_extra_height_px,
)


def test_16_bit_images_are_twice_as_large() -> None:
    assert image_bytes(1000, bit_depth=16) == 2 * image_bytes(1000, bit_depth=8)


def test_unsupported_bit_depth_is_rejected() -> None:
    with pytest.raises(ValueError, match="bit_depth"):
        image_bytes(1000, bit_depth=12)


def test_photo_count_rounds_up_to_cover_the_street() -> None:
    assert photo_count(10, 3) == 4


def test_invalid_length_is_rejected() -> None:
    with pytest.raises(ValueError, match="must be positive"):
        panorama_width_px(0, 0.00375)


def test_uphill_and_downhill_need_the_same_height() -> None:
    assert slope_extra_height_px(500, 0.05, 0.00375) == slope_extra_height_px(
        500, -0.05, 0.00375
    )


def test_image_smaller_than_one_tile_has_one_level() -> None:
    assert pyramid_levels(100, 100) == 1


@given(
    width=st.integers(min_value=1, max_value=10_000_000),
    height=st.integers(min_value=1, max_value=100_000),
)
def test_top_pyramid_level_fits_in_one_tile(width: int, height: int) -> None:
    levels = pyramid_levels(width, height)
    assert max(width, height) / 2 ** (levels - 1) <= 512


def test_visible_tiles_include_a_partly_scrolled_border() -> None:
    # 3840 / 512 = 7.5 columns and 2160 / 512 = 4.2 rows, plus one each for scrolling.
    assert max_visible_tiles(3840, 2160) == 9 * 6
