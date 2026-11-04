"""Render synthetic streets with exact ground truth for testing the pipeline.

Coordinates are in metres: X runs along the street, Y points up, and Z points from
the walking path towards the facade. The facade is a flat plane at a fixed Z, and
foreground objects are boxes. This is deliberately simple. It provides known camera
positions, depths and object outlines, not photorealism.

The world is left-handed, because image right, up and forward map directly to X, Y
and Z. COLMAP's world is right-handed, so aligning the two involves a mirror image.
"""

import json
from collections.abc import Iterator
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

import numpy as np
from numpy.typing import NDArray
from PIL import Image

from streetpano.geometry import focal_length_px

FORMAT_VERSION = 1

LABEL_SKY = 0
LABEL_GROUND = 1
LABEL_BUILDING_BASE = 100
LABEL_OBJECT_BASE = 1000

_WINDOW_SPACING_M = 2.5
_FLOOR_HEIGHT_M = 3.0
_SKY_RGB = (0.72, 0.82, 0.93)

FloatArray = NDArray[np.float64]
BoolArray = NDArray[np.bool_]


@dataclass(frozen=True)
class Building:
    """A section of the facade plane."""

    x_start_m: float
    x_end_m: float
    height_m: float
    color: tuple[float, float, float]


@dataclass(frozen=True)
class BoxObject:
    """A foreground object, standing on the ground in front of the facade."""

    name: str
    x_start_m: float
    x_end_m: float
    z_near_m: float
    z_far_m: float
    height_m: float
    color: tuple[float, float, float]
    speed_m_per_photo: float = 0.0

    @property
    def is_moving(self) -> bool:
        """Return whether the object changes position between photos."""
        return self.speed_m_per_photo != 0


@dataclass(frozen=True)
class CameraSpec:
    """A pinhole camera. Distortion is not modelled."""

    width_px: int = 1280
    height_px: int = 720
    focal_mm: float = 24.0
    sensor_width_mm: float = 36.0

    @property
    def focal_px(self) -> float:
        """Return the focal length in pixels."""
        return focal_length_px(self.focal_mm, self.sensor_width_mm, self.width_px)


@dataclass(frozen=True)
class StreetScene:
    """A complete description of a synthetic street and its capture."""

    length_m: float
    spacing_m: float
    buildings: tuple[Building, ...]
    objects: tuple[BoxObject, ...] = ()
    camera: CameraSpec = field(default_factory=CameraSpec)
    facade_distance_m: float = 15.0
    camera_height_m: float = 1.6
    slope: float = 0.0
    pitch_deg: float = 0.0
    roll_deg: float = 0.0
    path_wobble_m: float = 0.0
    exposure_jitter: float = 0.0
    seed: int = 0

    def __post_init__(self) -> None:
        """Reject scenes that can't be rendered."""
        if self.length_m < 0:
            raise ValueError(f"length_m must not be negative, got {self.length_m}")
        for name in ("spacing_m", "facade_distance_m", "camera_height_m"):
            if getattr(self, name) <= 0:
                raise ValueError(f"{name} must be positive, got {getattr(self, name)}")
        if self.camera.width_px <= 0 or self.camera.height_px <= 0:
            raise ValueError("camera dimensions must be positive")
        if not self.buildings:
            raise ValueError("a scene needs at least one building")
        for box in self.objects:
            if not 0 < box.z_near_m < box.z_far_m < self.facade_distance_m:
                raise ValueError(
                    f"object {box.name!r} must be between the path and the facade"
                )

    @property
    def photo_count(self) -> int:
        """Return the number of photos, including both ends of the street."""
        return int(self.length_m // self.spacing_m) + 1


@dataclass(frozen=True)
class Frame:
    """One rendered photo and its ground truth."""

    index: int
    image: NDArray[np.uint8]
    labels: NDArray[np.uint16]
    depth_m: NDArray[np.float32]


def default_scene(
    length_m: float = 30.0,
    spacing_m: float = 1.0,
    seed: int = 0,
    camera: CameraSpec | None = None,
    **options: Any,
) -> StreetScene:
    """Create a street with varied buildings, parked cars, trees and pedestrians.

    Buildings and objects extend past both ends of the street, so that the first and
    last photos have a full view. Other keyword arguments go to ``StreetScene``.
    """
    rng = np.random.default_rng(seed)
    margin_m = 40.0
    return StreetScene(
        length_m=length_m,
        spacing_m=spacing_m,
        buildings=_random_buildings(rng, -margin_m, length_m + margin_m),
        objects=_random_objects(rng, -margin_m, length_m + margin_m),
        camera=camera if camera is not None else CameraSpec(),
        seed=seed,
        **options,
    )


def _random_color(
    rng: np.random.Generator, low: float, high: float
) -> tuple[float, float, float]:
    red, green, blue = rng.uniform(low, high, 3)
    return (float(red), float(green), float(blue))


def _random_buildings(
    rng: np.random.Generator, start_m: float, end_m: float
) -> tuple[Building, ...]:
    buildings = []
    x_m = start_m
    while x_m < end_m:
        width_m = float(rng.uniform(8, 20))
        buildings.append(
            Building(
                x_start_m=x_m,
                x_end_m=x_m + width_m,
                height_m=float(rng.uniform(9, 20)),
                color=_random_color(rng, 0.45, 0.9),
            )
        )
        x_m += width_m
    return tuple(buildings)


def _random_objects(
    rng: np.random.Generator, start_m: float, end_m: float
) -> tuple[BoxObject, ...]:
    objects: list[BoxObject] = []
    x_m = start_m
    while x_m < end_m:
        x_m += float(rng.uniform(1, 12))
        length_m = 4.5
        objects.append(
            BoxObject(
                name=f"car-{len(objects)}",
                x_start_m=x_m,
                x_end_m=x_m + length_m,
                z_near_m=5.0,
                z_far_m=7.0,
                height_m=float(rng.uniform(1.4, 1.7)),
                color=_random_color(rng, 0.1, 0.8),
            )
        )
        x_m += length_m
    for x_m in np.arange(start_m, end_m, 12.0):
        tree_x_m = float(x_m) + float(rng.uniform(0, 6))
        objects.append(
            BoxObject(
                name=f"tree-{len(objects)}",
                x_start_m=tree_x_m,
                x_end_m=tree_x_m + float(rng.uniform(0.4, 1.0)),
                z_near_m=8.0,
                z_far_m=8.5,
                height_m=float(rng.uniform(4, 7)),
                color=(0.22, 0.4, 0.2),
            )
        )
    for _ in range(int((end_m - start_m) // 8)):
        x_m = float(rng.uniform(start_m, end_m))
        objects.append(
            BoxObject(
                name=f"person-{len(objects)}",
                x_start_m=x_m,
                x_end_m=x_m + 0.5,
                z_near_m=10.0,
                z_far_m=10.4,
                height_m=float(rng.uniform(1.6, 1.9)),
                color=_random_color(rng, 0.05, 0.9),
                speed_m_per_photo=float(rng.choice([-1.0, 1.0]) * rng.uniform(0.3, 1)),
            )
        )
    return tuple(objects)


def camera_position(scene: StreetScene, index: int) -> tuple[float, float, float]:
    """Return the camera's world position for a photo."""
    _require_valid_index(scene, index)
    x_m = index * scene.spacing_m
    wobble_y, wobble_z = 0.0, 0.0
    if scene.path_wobble_m > 0:
        rng = np.random.default_rng([scene.seed, index, 1])
        wobble_y, wobble_z = rng.normal(0, scene.path_wobble_m, 2)
    y_m = _ground_height(scene, x_m) + scene.camera_height_m + float(wobble_y)
    return (x_m, y_m, float(wobble_z))


def project_point(
    scene: StreetScene, index: int, point: tuple[float, float, float]
) -> tuple[float, float, float]:
    """Return a world point's pixel position (u, v) and its depth in metres."""
    position = np.array(camera_position(scene, index))
    camera_point = _rotation(scene).T @ (np.array(point) - position)
    x_cam, y_cam, depth_m = (float(value) for value in camera_point)
    if depth_m <= 0:
        raise ValueError("the point is behind the camera")
    focal = scene.camera.focal_px
    u = scene.camera.width_px / 2 + focal * x_cam / depth_m
    v = scene.camera.height_px / 2 - focal * y_cam / depth_m
    return (u, v, depth_m)


def render_frame(scene: StreetScene, index: int) -> Frame:
    """Render one photo with labels and depth."""
    origin = np.array(camera_position(scene, index))
    rays = _world_rays(scene)
    height, width = rays.shape[:2]

    distance = np.full((height, width), np.inf)
    labels = np.full((height, width), LABEL_SKY, dtype=np.uint16)
    image = np.empty((height, width, 3))
    image[:] = _SKY_RGB

    _draw_facade(scene, origin, rays, distance, labels, image)
    _draw_ground(scene, origin, rays, distance, labels, image)
    for object_index, box in enumerate(scene.objects):
        _draw_box(
            scene, box, object_index, index, origin, rays, distance, labels, image
        )

    if scene.exposure_jitter > 0:
        rng = np.random.default_rng([scene.seed, index, 2])
        image = image * (1 + rng.normal(0, scene.exposure_jitter))

    forward = _rotation(scene)[:, 2]
    depth = distance * (rays @ forward)
    return Frame(
        index=index,
        image=np.clip(np.round(image * 255), 0, 255).astype(np.uint8),
        labels=labels,
        depth_m=depth.astype(np.float32),
    )


def iter_frames(scene: StreetScene) -> Iterator[Frame]:
    """Render frames one at a time, so that memory use doesn't grow with length."""
    for index in range(scene.photo_count):
        yield render_frame(scene, index)


def write_dataset(scene: StreetScene, output_dir: Path) -> None:
    """Write photos, ground truth and a ``scene.json`` description to a new folder."""
    if output_dir.exists() and any(output_dir.iterdir()):
        raise FileExistsError(f"output folder is not empty: {output_dir}")
    photos_dir = output_dir / "photos"
    truth_dir = output_dir / "truth"
    photos_dir.mkdir(parents=True)
    truth_dir.mkdir()

    for frame in iter_frames(scene):
        name = f"{frame.index:05d}"
        Image.fromarray(frame.image).save(photos_dir / f"{name}.png")
        np.savez_compressed(
            truth_dir / f"{name}.npz", labels=frame.labels, depth_m=frame.depth_m
        )

    description = {
        "format_version": FORMAT_VERSION,
        "scene": asdict(scene),
        "focal_px": scene.camera.focal_px,
        "rotation_camera_to_world": _rotation(scene).tolist(),
        "camera_positions_m": [
            camera_position(scene, index) for index in range(scene.photo_count)
        ],
        "labels": {
            "sky": LABEL_SKY,
            "ground": LABEL_GROUND,
            "building_base": LABEL_BUILDING_BASE,
            "object_base": LABEL_OBJECT_BASE,
        },
    }
    (output_dir / "scene.json").write_text(
        json.dumps(description, indent=2) + "\n", encoding="utf-8"
    )


def _require_valid_index(scene: StreetScene, index: int) -> None:
    if not 0 <= index < scene.photo_count:
        raise IndexError(f"photo {index} is outside 0..{scene.photo_count - 1}")


def _ground_height(scene: StreetScene, x_m: float) -> float:
    return scene.slope * x_m


def _rotation(scene: StreetScene) -> FloatArray:
    """Return the camera-to-world rotation. Positive pitch looks upwards."""
    pitch, roll = np.radians(scene.pitch_deg), np.radians(scene.roll_deg)
    cos_p, sin_p = np.cos(pitch), np.sin(pitch)
    cos_r, sin_r = np.cos(roll), np.sin(roll)
    pitch_matrix = np.array([[1, 0, 0], [0, cos_p, sin_p], [0, -sin_p, cos_p]])
    roll_matrix = np.array([[cos_r, -sin_r, 0], [sin_r, cos_r, 0], [0, 0, 1]])
    rotation: FloatArray = pitch_matrix @ roll_matrix
    return rotation


def _world_rays(scene: StreetScene) -> FloatArray:
    camera = scene.camera
    u = np.arange(camera.width_px) + 0.5 - camera.width_px / 2
    v = camera.height_px / 2 - (np.arange(camera.height_px) + 0.5)
    grid_u, grid_v = np.meshgrid(u, v)
    focal = np.full(grid_u.shape, camera.focal_px)
    rays = np.stack([grid_u, grid_v, focal], axis=-1)
    rays /= np.linalg.norm(rays, axis=-1, keepdims=True)
    world: FloatArray = rays @ _rotation(scene).T
    return world


def _cell_noise(x: FloatArray, y: FloatArray, size_m: float, seed: int) -> FloatArray:
    """Return repeatable noise in 0..1, constant within each square cell."""
    cell_x = np.floor(x / size_m).astype(np.int64).astype(np.uint64)
    cell_y = np.floor(y / size_m).astype(np.int64).astype(np.uint64)
    mixed = (
        cell_x * np.uint64(0x9E3779B97F4A7C15)
        ^ cell_y * np.uint64(0xC2B2AE3D27D4EB4F)
        ^ np.uint64(((seed + 1) * 0x165667B19E3779F9) % 2**64)
    )
    mixed ^= mixed >> np.uint64(29)
    mixed *= np.uint64(0xBF58476D1CE4E5B9)
    mixed ^= mixed >> np.uint64(32)
    noise: FloatArray = (mixed & np.uint64(0xFFFF)).astype(np.float64) / 0xFFFF
    return noise


def _texture(x: FloatArray, y: FloatArray, seed: int) -> FloatArray:
    """Return brightness that varies at several scales, so that photos can match."""
    return (
        0.75
        + 0.3 * (_cell_noise(x, y, 1.1, seed) - 0.5)
        + 0.3 * (_cell_noise(x, y, 0.37, seed + 1) - 0.5)
        + 0.15 * (_cell_noise(x, y, 0.13, seed + 2) - 0.5)
    )


def _draw_facade(
    scene: StreetScene,
    origin: FloatArray,
    rays: FloatArray,
    distance: FloatArray,
    labels: NDArray[np.uint16],
    image: FloatArray,
) -> None:
    toward = rays[..., 2] > 0
    t = np.full(distance.shape, np.inf)
    t[toward] = (scene.facade_distance_m - origin[2]) / rays[..., 2][toward]
    t[~toward] = np.inf
    finite = np.isfinite(t)
    x = origin[0] + np.where(finite, t, 0.0) * rays[..., 0]
    y = origin[1] + np.where(finite, t, 0.0) * rays[..., 1]

    for building_index, building in enumerate(scene.buildings):
        centre_m = (building.x_start_m + building.x_end_m) / 2
        base_m = _ground_height(scene, centre_m)
        mask = (
            finite
            & (x >= building.x_start_m)
            & (x < building.x_end_m)
            & (y >= scene.slope * x)
            & (y <= base_m + building.height_m)
        )
        if not mask.any():
            continue
        hit_x, hit_y = x[mask], y[mask]
        seed = scene.seed + 10 * building_index
        shade = _texture(hit_x, hit_y, seed)
        color = np.outer(shade, building.color)

        across = np.mod(hit_x - building.x_start_m, _WINDOW_SPACING_M)
        level = np.mod(hit_y - base_m, _FLOOR_HEIGHT_M)
        window = (across > 0.65) & (across < 1.85) & (level > 0.9) & (level < 2.5)
        tint = 0.08 + 0.3 * _cell_noise(hit_x, hit_y, _WINDOW_SPACING_M, seed + 5)
        color[window] = np.outer(tint[window], (0.8, 0.9, 1.0))

        image[mask] = color
        distance[mask] = t[mask]
        labels[mask] = LABEL_BUILDING_BASE + building_index


def _draw_ground(
    scene: StreetScene,
    origin: FloatArray,
    rays: FloatArray,
    distance: FloatArray,
    labels: NDArray[np.uint16],
    image: FloatArray,
) -> None:
    denominator = rays[..., 1] - scene.slope * rays[..., 0]
    valid = denominator < 0
    t = np.full(distance.shape, np.inf)
    t[valid] = (scene.slope * origin[0] - origin[1]) / denominator[valid]
    hit_z = origin[2] + np.where(np.isfinite(t), t, 0.0) * rays[..., 2]
    mask = (
        (t > 0) & np.isfinite(t) & (hit_z <= scene.facade_distance_m) & (t < distance)
    )
    if not mask.any():
        return
    hit_x = origin[0] + t[mask] * rays[..., 0][mask]
    shade = _texture(hit_x, hit_z[mask], scene.seed + 7)
    image[mask] = np.outer(shade, (0.42, 0.42, 0.44))
    distance[mask] = t[mask]
    labels[mask] = LABEL_GROUND


def _draw_box(
    scene: StreetScene,
    box: BoxObject,
    object_index: int,
    photo_index: int,
    origin: FloatArray,
    rays: FloatArray,
    distance: FloatArray,
    labels: NDArray[np.uint16],
    image: FloatArray,
) -> None:
    offset_m = box.speed_m_per_photo * photo_index
    x_start = box.x_start_m + offset_m
    x_end = box.x_end_m + offset_m
    base_m = _ground_height(scene, (x_start + x_end) / 2)
    low = np.array([x_start, base_m, box.z_near_m])
    high = np.array([x_end, base_m + box.height_m, box.z_far_m])

    entry, exit_ = _ray_box_interval(origin, rays, low, high)
    t = np.where(entry > 0, entry, exit_)
    mask = (exit_ >= entry) & (exit_ > 0) & (t < distance)
    if not mask.any():
        return
    hit = origin + t[mask][:, None] * rays[mask]
    seed = scene.seed + 1000 + object_index
    shade = _texture(hit[:, 0] - offset_m, hit[:, 1] + hit[:, 2], seed)
    image[mask] = np.outer(shade, box.color)
    distance[mask] = t[mask]
    labels[mask] = LABEL_OBJECT_BASE + object_index


def _ray_box_interval(
    origin: FloatArray, rays: FloatArray, low: FloatArray, high: FloatArray
) -> tuple[FloatArray, FloatArray]:
    """Return where each ray enters and leaves a box, using the slab method."""
    parallel = rays == 0
    safe_rays = np.where(parallel, 1.0, rays)
    near = (low - origin) / safe_rays
    far = (high - origin) / safe_rays
    outside = parallel & ((origin < low) | (origin > high))
    slab_entry = np.where(parallel, -np.inf, np.minimum(near, far))
    slab_exit = np.where(parallel, np.inf, np.maximum(near, far))
    slab_exit = np.where(outside, -np.inf, slab_exit)
    return slab_entry.max(axis=-1), slab_exit.min(axis=-1)
