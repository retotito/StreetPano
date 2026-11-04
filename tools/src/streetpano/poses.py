"""Find camera positions from photos with COLMAP, and measure them against truth.

Focal length is always held fixed. On a flat facade photographed along a nearly
straight line, a longer lens and a more distant wall look almost identical, so
letting COLMAP adjust the focal length gives wrong depths. See decision record 0008.
"""

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from numpy.typing import NDArray
from PIL import Image

FloatArray = NDArray[np.float64]

# COLMAP cameras have Y pointing down. Synthetic cameras have Y pointing up.
_FLIP_Y = np.diag([1.0, -1.0, 1.0])
_PHOTO_SUFFIXES = {".jpg", ".jpeg", ".png", ".tif", ".tiff"}


class PoseEstimationError(RuntimeError):
    """COLMAP couldn't build a reconstruction from the photos."""


@dataclass(frozen=True)
class CameraPose:
    """One photo's position and orientation in the reconstruction's own world."""

    name: str
    center: tuple[float, float, float]
    rotation_world_to_camera: tuple[tuple[float, float, float], ...]


@dataclass(frozen=True)
class PoseEstimate:
    """The result of a reconstruction. Its scale and origin are arbitrary."""

    poses: tuple[CameraPose, ...]
    points: FloatArray
    focal_px: float
    missing: tuple[str, ...]
    mean_reprojection_error_px: float


@dataclass(frozen=True)
class AccuracyReport:
    """How closely an estimate matches a synthetic street's exact truth."""

    registered: int
    total: int
    rotation_median_deg: float
    rotation_max_deg: float
    position_median_m: float
    position_max_m: float
    facade_distance_m: float


def list_photos(photo_dir: Path) -> list[Path]:
    """Return a folder's photos in name order."""
    if not photo_dir.is_dir():
        raise ValueError(f"photo folder not found: {photo_dir}")
    photos = sorted(
        path for path in photo_dir.iterdir() if path.suffix.lower() in _PHOTO_SUFFIXES
    )
    if len(photos) < 2:
        raise ValueError(f"need at least 2 photos in {photo_dir}, found {len(photos)}")
    return photos


def estimate_poses(photo_dir: Path, work_dir: Path, focal_px: float) -> PoseEstimate:
    """Reconstruct camera positions, using the known focal length.

    ``work_dir`` must be new or empty. All photos must come from one camera at one
    zoom setting, and lens distortion must already be removed.
    """
    import pycolmap

    if focal_px <= 0:
        raise ValueError(f"focal_px must be positive, got {focal_px}")
    photos = list_photos(photo_dir)
    if work_dir.exists() and any(work_dir.iterdir()):
        raise FileExistsError(f"work folder is not empty: {work_dir}")
    work_dir.mkdir(parents=True, exist_ok=True)

    with Image.open(photos[0]) as first:
        width, height = first.size

    reader = pycolmap.ImageReaderOptions()
    reader.camera_model = "PINHOLE"
    reader.camera_params = f"{focal_px},{focal_px},{width / 2},{height / 2}"
    database = work_dir / "database.db"
    pycolmap.extract_features(
        database,
        photo_dir,
        camera_mode=pycolmap.CameraMode.SINGLE,
        reader_options=reader,
        device=pycolmap.Device.cpu,
    )
    pycolmap.match_sequential(database, device=pycolmap.Device.cpu)

    options = pycolmap.IncrementalPipelineOptions()
    options.ba_refine_focal_length = False
    options.ba_refine_principal_point = False
    options.ba_refine_extra_params = False
    reconstructions = pycolmap.incremental_mapping(
        database, photo_dir, work_dir / "sparse", options=options
    )
    if not reconstructions:
        raise PoseEstimationError("COLMAP couldn't connect the photos")

    # A broken sequence can produce several parts. Keep the largest, and report the
    # other photos as missing instead of hiding them.
    largest = max(reconstructions.values(), key=lambda item: item.num_reg_images())
    poses = []
    for image in largest.images.values():
        if image.has_pose:
            rotation = image.cam_from_world().rotation.matrix()
            poses.append(
                CameraPose(
                    name=image.name,
                    center=_vector(image.projection_center()),
                    rotation_world_to_camera=tuple(_vector(row) for row in rotation),
                )
            )
    poses.sort(key=lambda pose: pose.name)
    registered = {pose.name for pose in poses}
    return PoseEstimate(
        poses=tuple(poses),
        points=np.array([point.xyz for point in largest.points3D.values()]),
        focal_px=focal_px,
        missing=tuple(p.name for p in photos if p.name not in registered),
        mean_reprojection_error_px=float(largest.compute_mean_reprojection_error()),
    )


def compare_with_truth(
    estimate: PoseEstimate, description: dict[str, Any]
) -> AccuracyReport:
    """Align an estimate to a synthetic street's ``scene.json`` and measure the error.

    The alignment uses rotations first, because cameras along a nearly straight line
    leave positions free to spin around the street's axis.
    """
    if not estimate.poses:
        raise ValueError("the estimate has no poses")
    true_positions = np.array(description["camera_positions_m"])
    camera_to_world = np.array(description["rotation_camera_to_world"])
    indices = [int(Path(pose.name).stem) for pose in estimate.poses]

    candidates = [
        camera_to_world @ _FLIP_Y @ np.array(pose.rotation_world_to_camera)
        for pose in estimate.poses
    ]
    left, _, right = np.linalg.svd(np.sum(candidates, axis=0))
    alignment = left @ right
    angles = [_rotation_angle_deg(alignment.T @ item) for item in candidates]

    centers = (alignment @ np.array([pose.center for pose in estimate.poses]).T).T
    truth = true_positions[indices]
    center_mean, truth_mean = centers.mean(axis=0), truth.mean(axis=0)
    spread = float(((centers - center_mean) ** 2).sum())
    scale = (
        ((truth - truth_mean) * (centers - center_mean)).sum() / spread if spread else 1
    )
    aligned = scale * (centers - center_mean) + truth_mean
    errors = np.linalg.norm(aligned - truth, axis=1)

    world_points = (
        scale * ((alignment @ estimate.points.T).T - center_mean) + truth_mean
    )
    return AccuracyReport(
        registered=len(estimate.poses),
        total=len(true_positions),
        rotation_median_deg=float(np.median(angles)),
        rotation_max_deg=float(np.max(angles)),
        position_median_m=float(np.median(errors)),
        position_max_m=float(errors.max()),
        facade_distance_m=_densest_depth(world_points[:, 2]),
    )


def write_estimate(estimate: PoseEstimate, path: Path) -> None:
    """Save camera poses as JSON."""
    content = {
        "format_version": 1,
        "focal_px": estimate.focal_px,
        "missing": list(estimate.missing),
        "mean_reprojection_error_px": estimate.mean_reprojection_error_px,
        "poses": [
            {
                "name": pose.name,
                "center": pose.center,
                "rotation_world_to_camera": pose.rotation_world_to_camera,
            }
            for pose in estimate.poses
        ],
    }
    path.write_text(json.dumps(content, indent=2) + "\n", encoding="utf-8")


def _vector(values: Any) -> tuple[float, float, float]:
    x, y, z = (float(value) for value in values)
    return (x, y, z)


def _rotation_angle_deg(rotation: FloatArray) -> float:
    cosine = (np.trace(rotation) - 1) / 2
    return float(np.degrees(np.arccos(np.clip(cosine, -1, 1))))


def _densest_depth(depths: FloatArray) -> float:
    """Return the depth where most points lie, which is the facade on a street."""
    finite = depths[np.isfinite(depths)]
    if finite.size == 0:
        return float("nan")
    bins = np.arange(finite.min(), finite.max() + 0.5, 0.25)
    counts, edges = np.histogram(finite, bins=bins)
    peak = edges[counts.argmax()] + 0.125
    return float(np.median(finite[np.abs(finite - peak) < 0.5]))
