"""Capture geometry: how far things shift between photos and what stays hidden.

All distances are in metres and measured from the camera path unless stated
otherwise. These functions use a pinhole camera model.
"""


def _require_positive(**values: float) -> None:
    for name, value in values.items():
        if value <= 0:
            raise ValueError(f"{name} must be positive, got {value}")


def focal_length_px(
    focal_mm: float, sensor_width_mm: float, image_width_px: int
) -> float:
    """Return the focal length in pixels."""
    _require_positive(
        focal_mm=focal_mm,
        sensor_width_mm=sensor_width_mm,
        image_width_px=image_width_px,
    )
    return image_width_px * focal_mm / sensor_width_mm


def parallax_shift_px(
    focal_px: float,
    spacing_m: float,
    object_distance_m: float,
    facade_distance_m: float,
) -> float:
    """Return how far an object moves against the facade between two photos.

    The result is zero for a point on the facade, and it grows quickly as the
    object gets closer to the camera.
    """
    _require_positive(
        focal_px=focal_px,
        spacing_m=spacing_m,
        object_distance_m=object_distance_m,
        facade_distance_m=facade_distance_m,
    )
    if object_distance_m > facade_distance_m:
        raise ValueError("the object must not be behind the facade")
    return focal_px * spacing_m * (1 / object_distance_m - 1 / facade_distance_m)


def ground_sample_distance_m(facade_distance_m: float, focal_px: float) -> float:
    """Return the size of one pixel on the facade, in metres."""
    _require_positive(facade_distance_m=facade_distance_m, focal_px=focal_px)
    return facade_distance_m / focal_px


def hidden_facade_height_m(
    camera_height_m: float,
    obstacle_height_m: float,
    obstacle_near_m: float,
    obstacle_far_m: float,
    facade_distance_m: float,
) -> float:
    """Return how much of the facade an obstacle hides from every position.

    This applies to a long row of obstacles, such as parked cars, where walking
    along the street gives no view past them. ``obstacle_near_m`` and
    ``obstacle_far_m`` are the distances to the obstacle's two sides. The result
    is measured upwards from the ground and is never negative.
    """
    _require_positive(
        camera_height_m=camera_height_m,
        obstacle_height_m=obstacle_height_m,
        obstacle_near_m=obstacle_near_m,
        facade_distance_m=facade_distance_m,
    )
    if not obstacle_near_m <= obstacle_far_m < facade_distance_m:
        raise ValueError("expected near <= far < facade distance")

    # A camera above the obstacle looks down, so the far side limits the view.
    # A camera below it looks up, so the near side limits the view.
    limiting_edge_m = (
        obstacle_far_m if camera_height_m > obstacle_height_m else obstacle_near_m
    )
    slope = (obstacle_height_m - camera_height_m) / limiting_edge_m
    return max(0.0, camera_height_m + slope * facade_distance_m)
