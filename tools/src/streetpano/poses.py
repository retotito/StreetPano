"""Find camera positions from photos with COLMAP, and measure them against truth.

Focal length is always held fixed. On a flat facade photographed along a nearly
straight line, a longer lens and a more distant wall look almost identical, so
letting COLMAP adjust the focal length gives wrong depths. See decision record 0008.
"""

import itertools
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
_EXIF_ORIENTATION = 0x0112
_EXIF_IFD = 0x8769
_EXIF_FOCAL_35MM = 0xA405
_FULL_FRAME_DIAGONAL_MM = 43.27
_UP_BY_ORIENTATION: dict[int, tuple[float, float, float]] = {
    1: (0.0, -1.0, 0.0),
    3: (0.0, 1.0, 0.0),
    6: (-1.0, 0.0, 0.0),
    8: (1.0, 0.0, 0.0),
}
_MAX_POINTS_IN_FILE = 20000

# COLMAP's defaults are made for unordered photo collections. Photos taken along a
# street only need to match their neighbours. These settings took 16 photos of 12 MP
# from 417 s to 9.5 s on an 8 GB laptop, with the same accuracy on a synthetic
# street. They sit above the lowest values that worked, because real facades are
# harder to match.
_MAX_IMAGE_SIZE_PX = 2000
_MAX_FEATURES = 5000
_NEIGHBOUR_PHOTOS = 8

# On a real street, COLMAP can split the photos at a weak link, and part of the result
# differs from run to run. Repeating this quick step and checking each result costs
# little, because the slow matching runs only once.
_MAPPING_ATTEMPTS = 5

# Limits for a believable walk, as multiples of the usual step. False matches between
# repeating windows can give a low reprojection error and an impossible path, so that
# error alone can't be trusted.
_MAX_STEP_RATIO = 3.0
_MIN_STEP_RATIO = 0.1
_MAX_SHORT_STEP_SHARE = 0.2
_MAX_TURN_DEG = 30.0
_MAX_PACE_CHANGE = 1.5
_MIN_STEPS_PER_SIDE = 3
_MIN_SECTION_PHOTOS = 2

# Limits for joining two sections at a gap.
_MIN_JOIN_MATCHES = 20
_MAX_JOIN_GAP_PHOTOS = 2
_MAX_JOIN_ANGLE_DEG = 45.0


class PoseEstimationError(RuntimeError):
    """COLMAP couldn't build a reconstruction from the photos."""


@dataclass(frozen=True)
class CameraPose:
    """One photo's position and orientation in the reconstruction's own world."""

    name: str
    center: tuple[float, float, float]
    rotation_world_to_camera: tuple[tuple[float, float, float], ...]
    up_in_camera: tuple[float, float, float] = (0.0, -1.0, 0.0)


@dataclass(frozen=True)
class PoseEstimate:
    """The result of a reconstruction. Its scale and origin are arbitrary."""

    poses: tuple[CameraPose, ...]
    points: FloatArray
    focal_px: float
    missing: tuple[str, ...]
    mean_reprojection_error_px: float
    warnings: tuple[str, ...] = ()


@dataclass(frozen=True)
class Section:
    """An unbroken run of photos that COLMAP placed together, in photo order."""

    poses: tuple[CameraPose, ...]
    points: FloatArray
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


def up_in_camera(photo: Path) -> tuple[float, float, float]:
    """Return which way is up in a photo's stored pixels, from its orientation tag.

    COLMAP ignores orientation tags, so a phone held another way round stores its pixels
    rotated, and only this tag says which way up they belong.
    """
    with Image.open(photo) as image:
        orientation = image.getexif().get(_EXIF_ORIENTATION, 1)
    return _UP_BY_ORIENTATION.get(orientation, (0.0, -1.0, 0.0))


def focal_from_metadata(photo: Path) -> float:
    """Return the focal length in pixels, from the 35 mm equivalent in its metadata."""
    with Image.open(photo) as image:
        width, height = image.size
        exif = image.getexif().get_ifd(_EXIF_IFD)
    equivalent_mm = exif.get(_EXIF_FOCAL_35MM)
    if not equivalent_mm:
        raise ValueError(
            f"{photo.name} doesn't record its focal length, so please give --focal-px"
        )
    # A 35 mm equivalent is defined by the diagonal, whatever the sensor's shape.
    diagonal_px = float(np.hypot(width, height))
    return float(equivalent_mm) * diagonal_px / _FULL_FRAME_DIAGONAL_MM


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
    zoom setting, and lens distortion must already be removed. COLMAP's result varies
    from run to run on real streets, so this one can vary too.
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
    extraction = pycolmap.FeatureExtractionOptions()
    extraction.max_image_size = _MAX_IMAGE_SIZE_PX
    extraction.sift.max_num_features = _MAX_FEATURES
    extraction.sift.first_octave = 0
    pycolmap.extract_features(
        database,
        photo_dir,
        camera_mode=pycolmap.CameraMode.SINGLE,
        reader_options=reader,
        extraction_options=extraction,
        device=pycolmap.Device.cpu,
    )
    pairing = pycolmap.SequentialPairingOptions()
    pairing.overlap = _NEIGHBOUR_PHOTOS
    pairing.quadratic_overlap = False
    pycolmap.match_sequential(
        database, pairing_options=pairing, device=pycolmap.Device.cpu
    )

    order = {photo.name: index for index, photo in enumerate(photos)}
    best: Section | None = None
    fallback: Section | None = None
    warnings: list[str] = []
    for attempt in range(_MAPPING_ATTEMPTS):
        sections = _map_sections(pycolmap, database, photo_dir, work_dir, attempt)
        good = select_sections(
            [section for section in sections if not section_problems(section)], order
        )
        joined = _connect_sections(pycolmap, database, good, order)
        if joined is not None and (best is None or _quality(joined) > _quality(best)):
            best = joined
        for section in sections:
            if fallback is None or len(section.poses) > len(fallback.poses):
                fallback = section
        if best is not None and len(best.poses) == len(photos):
            break
    if best is None:
        if fallback is None:
            raise PoseEstimationError("COLMAP couldn't connect the photos")
        best = fallback
        warnings = [
            "The camera path looks wrong, so please check the result: "
            + "; ".join(section_problems(fallback))
        ]

    registered = {pose.name for pose in best.poses}
    return PoseEstimate(
        poses=best.poses,
        points=best.points,
        focal_px=focal_px,
        missing=tuple(p.name for p in photos if p.name not in registered),
        mean_reprojection_error_px=best.mean_reprojection_error_px,
        warnings=tuple(warnings),
    )


def path_problems(centers: FloatArray, forwards: FloatArray) -> list[str]:
    """Return why a walk along a street is impossible, or nothing if it's believable.

    Photos must be in order, and they're expected to be roughly evenly spaced.
    """
    if len(centers) < 3:
        return []
    steps = np.linalg.norm(np.diff(centers, axis=0), axis=1)
    usual = float(np.median(steps))
    if usual <= 0:
        return ["the cameras don't move"]
    problems = []
    if steps.max() > _MAX_STEP_RATIO * usual:
        problems.append(f"one step is {steps.max() / usual:.0f} times the usual size")
    if np.mean(steps < _MIN_STEP_RATIO * usual) > _MAX_SHORT_STEP_SHARE:
        problems.append("many cameras are in almost the same place")
    cosines = np.clip(np.sum(forwards[1:] * forwards[:-1], axis=1), -1, 1)
    turn = float(np.degrees(np.arccos(cosines)).max())
    if turn > _MAX_TURN_DEG:
        problems.append(f"the camera turns {turn:.0f} degrees between two photos")
    for split in range(_MIN_STEPS_PER_SIDE, len(steps) - _MIN_STEPS_PER_SIDE + 1):
        before = float(np.median(steps[:split]))
        after = float(np.median(steps[split:]))
        if max(before, after) > _MAX_PACE_CHANGE * min(before, after):
            problems.append("the step size changes suddenly along the street")
            break
    return problems


def split_points(centers: FloatArray, forwards: FloatArray) -> list[int]:
    """Return the photos after which the path breaks: an odd step or a sudden turn."""
    if len(centers) < 3:
        return []
    steps = np.linalg.norm(np.diff(centers, axis=0), axis=1)
    usual = float(np.median(steps))
    cosines = np.clip(np.sum(forwards[1:] * forwards[:-1], axis=1), -1, 1)
    turns = np.degrees(np.arccos(cosines))
    breaks = (
        (steps > _MAX_STEP_RATIO * usual)
        | (steps < usual / _MAX_STEP_RATIO)
        | (turns > _MAX_TURN_DEG)
    )
    return [int(index) for index in np.flatnonzero(breaks)]


def section_problems(section: Section) -> list[str]:
    """Return why a section's path is impossible, or nothing if it's believable."""
    centers = np.array([pose.center for pose in section.poses], dtype=np.float64)
    forwards = np.array(
        [pose.rotation_world_to_camera[2] for pose in section.poses], dtype=np.float64
    )
    return path_problems(centers, forwards)


def select_sections(sections: list[Section], order: dict[str, int]) -> list[Section]:
    """Keep the largest sections that don't share photos, in photo order."""
    used: set[str] = set()
    chosen = []
    for section in sorted(sections, key=lambda item: -len(item.poses)):
        names = {pose.name for pose in section.poses}
        if not names & used:
            used |= names
            chosen.append(section)
    return sorted(chosen, key=lambda item: order[item.poses[0].name])


def join_sections(
    first: Section,
    second: Section,
    rotation_second_from_first: FloatArray,
    direction_in_first: FloatArray,
    photo_gap: int,
) -> Section | None:
    """Move ``second`` into the world of ``first``, or return None if they don't fit.

    The rotation and direction relate the last photo of ``first`` to the first photo of
    ``second``, in the coordinates of that last photo. Two photos can't give the length
    of the step between them, or the sections' relative scale. Both come from the usual
    step in each section, which assumes a steady walking pace across the gap. Measuring
    the distance to the buildings instead proved too noisy, because a tree or a van can
    dominate a single photo.
    """
    if len(first.poses) < 2 or len(second.poses) < 2:
        return None
    last = first.poses[-1]
    start = second.poses[0]
    last_rotation = np.array(last.rotation_world_to_camera)
    last_center = np.array(last.center)
    start_center = np.array(start.center)

    first_centers = np.array([pose.center for pose in first.poses])
    first_step = _usual_step(first_centers)
    second_step = _usual_step(np.array([pose.center for pose in second.poses]))
    if first_step <= 0 or second_step <= 0:
        return None
    scale = first_step / second_step

    direction = last_rotation.T @ direction_in_first
    walking = last_center - first_centers[-2]
    cosine = float(
        np.dot(direction, walking)
        / (np.linalg.norm(direction) * np.linalg.norm(walking))
    )
    if float(np.degrees(np.arccos(np.clip(cosine, -1, 1)))) > _MAX_JOIN_ANGLE_DEG:
        return None
    new_start = last_center + direction / np.linalg.norm(direction) * (
        first_step * photo_gap
    )

    # Takes directions in the second section's world to the first section's world.
    start_rotation = np.array(start.rotation_world_to_camera)
    world = (rotation_second_from_first @ last_rotation).T @ start_rotation

    def moved(point: FloatArray) -> FloatArray:
        return np.asarray(
            new_start + scale * world @ (point - start_center), dtype=np.float64
        )

    moved_poses = tuple(
        CameraPose(
            name=pose.name,
            center=_vector(moved(np.array(pose.center))),
            rotation_world_to_camera=tuple(
                _vector(row)
                for row in np.array(pose.rotation_world_to_camera) @ world.T
            ),
            up_in_camera=pose.up_in_camera,
        )
        for pose in second.poses
    )
    moved_points = (
        new_start + scale * (second.points - start_center) @ world.T
        if len(second.points)
        else second.points
    )
    total_points = len(first.points) + len(second.points)
    error = (
        first.mean_reprojection_error_px * len(first.points)
        + second.mean_reprojection_error_px * len(second.points)
    ) / max(total_points, 1)
    return Section(
        poses=first.poses + moved_poses,
        points=np.concatenate([first.points, moved_points]),
        mean_reprojection_error_px=float(error),
    )


def _map_sections(
    pycolmap: Any, database: Path, photo_dir: Path, work_dir: Path, attempt: int
) -> list[Section]:
    options = pycolmap.IncrementalPipelineOptions()
    options.ba_refine_focal_length = False
    options.ba_refine_principal_point = False
    options.ba_refine_extra_params = False
    output = work_dir / "sparse" / str(attempt)
    output.mkdir(parents=True)
    reconstructions = pycolmap.incremental_mapping(
        database, photo_dir, output, options=options
    )
    sections: list[Section] = []
    for reconstruction in reconstructions.values():
        sections += _sections_from(reconstruction, photo_dir)
    return sections


def _sections_from(reconstruction: Any, photo_dir: Path) -> list[Section]:
    """Split a reconstruction wherever its path breaks, and keep the usable parts."""
    images = sorted(
        (image for image in reconstruction.images.values() if image.has_pose),
        key=lambda image: image.name,
    )
    if not images:
        return []
    centers = np.array([image.projection_center() for image in images])
    forwards = np.array(
        [image.cam_from_world().rotation.matrix()[2] for image in images]
    )
    starts = [0, *(index + 1 for index in split_points(centers, forwards)), len(images)]
    return [
        _section_from(reconstruction, images[start:end], photo_dir)
        for start, end in itertools.pairwise(starts)
        if end - start >= _MIN_SECTION_PHOTOS
    ]


def _section_from(reconstruction: Any, images: list[Any], photo_dir: Path) -> Section:
    poses = []
    for image in images:
        rotation = image.cam_from_world().rotation.matrix()
        poses.append(
            CameraPose(
                name=image.name,
                center=_vector(image.projection_center()),
                rotation_world_to_camera=tuple(_vector(row) for row in rotation),
                up_in_camera=up_in_camera(photo_dir / image.name),
            )
        )
    ids = {image.image_id for image in images}
    return Section(
        poses=tuple(poses),
        points=np.array(
            [
                point.xyz
                for point in reconstruction.points3D.values()
                if any(item.image_id in ids for item in point.track.elements)
            ],
            dtype=np.float64,
        ).reshape(-1, 3),
        mean_reprojection_error_px=float(
            reconstruction.compute_mean_reprojection_error()
        ),
    )


def _quality(section: Section) -> tuple[int, float]:
    """Rank results by the photos they cover, then by how even their steps are."""
    centers = np.array([pose.center for pose in section.poses])
    steps = np.linalg.norm(np.diff(centers, axis=0), axis=1)
    return len(section.poses), -float(np.std(steps) / max(np.mean(steps), 1e-12))


def _usual_step(centers: FloatArray) -> float:
    return float(np.median(np.linalg.norm(np.diff(centers, axis=0), axis=1)))


def _connect_sections(
    pycolmap: Any, database: Path, sections: list[Section], order: dict[str, int]
) -> Section | None:
    """Join neighbouring sections where possible, and return the longest result."""
    if not sections:
        return None
    joined = [sections[0]]
    for section in sections[1:]:
        result = _join_at_gap(pycolmap, database, joined[-1], section, order)
        if result is None or section_problems(result):
            joined.append(section)
        else:
            joined[-1] = result
    return max(joined, key=lambda item: len(item.poses))


def _join_at_gap(
    pycolmap: Any,
    database: Path,
    first: Section,
    second: Section,
    order: dict[str, int],
) -> Section | None:
    last_name = first.poses[-1].name
    start_name = second.poses[0].name
    gap = order[start_name] - order[last_name]
    if gap > _MAX_JOIN_GAP_PHOTOS:
        return None
    relative = _relative_pose(pycolmap, database, last_name, start_name)
    if relative is None:
        return None
    return join_sections(first, second, relative[0], relative[1], gap)


def _relative_pose(
    pycolmap: Any, database: Path, first_name: str, second_name: str
) -> tuple[FloatArray, FloatArray] | None:
    """Return the rotation and walking direction between two photos' cameras."""
    with pycolmap.Database.open(database) as store:
        first = store.read_image_with_name(first_name)
        second = store.read_image_with_name(second_name)
        matches = store.read_two_view_geometry(
            first.image_id, second.image_id
        ).inlier_matches
        if len(matches) < _MIN_JOIN_MATCHES:
            return None
        first_points = store.read_keypoints(first.image_id)[matches[:, 0], :2]
        second_points = store.read_keypoints(second.image_id)[matches[:, 1], :2]
        camera = store.read_camera(first.camera_id)
    result = pycolmap.estimate_essential_matrix(
        first_points.astype(np.float64),
        second_points.astype(np.float64),
        camera,
        camera,
    )
    if result is None:
        return None
    pose = result["cam2_from_cam1"]
    rotation = np.array(pose.rotation.matrix())
    return rotation, -rotation.T @ np.array(pose.translation)


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
    points = estimate.points
    if len(points) > _MAX_POINTS_IN_FILE:
        step = len(points) // _MAX_POINTS_IN_FILE + 1
        points = points[::step]
    content = {
        "format_version": 2,
        "focal_px": estimate.focal_px,
        "missing": list(estimate.missing),
        "mean_reprojection_error_px": estimate.mean_reprojection_error_px,
        "warnings": list(estimate.warnings),
        "poses": [
            {
                "name": pose.name,
                "center": pose.center,
                "rotation_world_to_camera": pose.rotation_world_to_camera,
                "up_in_camera": pose.up_in_camera,
            }
            for pose in estimate.poses
        ],
        "points": points.tolist(),
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
