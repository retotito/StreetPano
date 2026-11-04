from collections.abc import Callable

import pytest
from hypothesis import given
from hypothesis import strategies as st

from streetpano.geometry import (
    focal_length_px,
    ground_sample_distance_m,
    hidden_facade_height_m,
    parallax_shift_px,
)

distance = st.floats(min_value=0.5, max_value=100)


def test_focal_length_px_for_full_frame_24mm() -> None:
    assert focal_length_px(24, 36, 6000) == pytest.approx(4000)


def test_point_on_facade_has_no_parallax() -> None:
    assert parallax_shift_px(4000, 3, 15, 15) == 0


def test_object_behind_facade_is_rejected() -> None:
    with pytest.raises(ValueError, match="behind the facade"):
        parallax_shift_px(4000, 3, 16, 15)


@pytest.mark.parametrize(
    "call",
    [
        lambda: focal_length_px(0, 36, 6000),
        lambda: parallax_shift_px(4000, -1, 5, 15),
        lambda: ground_sample_distance_m(0, 4000),
        lambda: hidden_facade_height_m(0, 1.5, 5, 7, 15),
    ],
)
def test_non_positive_values_are_rejected(call: Callable[[], float]) -> None:
    with pytest.raises(ValueError, match="must be positive"):
        call()


@given(near=distance, gap=st.floats(min_value=0.1, max_value=50))
def test_closer_objects_shift_more(near: float, gap: float) -> None:
    facade = near + gap + 1
    closer = parallax_shift_px(4000, 1, near, facade)
    farther = parallax_shift_px(4000, 1, near + gap, facade)
    assert closer > farther


@given(spacing=st.floats(min_value=0.1, max_value=10))
def test_parallax_is_proportional_to_spacing(spacing: float) -> None:
    single = parallax_shift_px(4000, spacing, 5, 15)
    double = parallax_shift_px(4000, spacing * 2, 5, 15)
    assert double == pytest.approx(2 * single)


def test_raising_the_camera_clears_parked_cars() -> None:
    assert hidden_facade_height_m(3, 1.5, 5, 7, 15) == 0


def test_camera_below_obstacle_uses_near_edge() -> None:
    # Looking up from 1 m over a 2 m wall 5 m away: 1 + (1 / 5) * 15 = 4 m hidden.
    assert hidden_facade_height_m(1, 2, 5, 7, 15) == pytest.approx(4)


def test_invalid_obstacle_position_is_rejected() -> None:
    with pytest.raises(ValueError, match="near <= far < facade"):
        hidden_facade_height_m(1.6, 1.5, 7, 5, 15)
    with pytest.raises(ValueError, match="near <= far < facade"):
        hidden_facade_height_m(1.6, 1.5, 5, 15, 15)
