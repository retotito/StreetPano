import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest
from PIL import Image

from streetpano.poses import (
    AccuracyReport,
    CameraPose,
    PoseEstimate,
    PoseEstimationError,
    compare_with_truth,
    estimate_poses,
    focal_from_metadata,
    list_photos,
    up_in_camera,
    write_estimate,
)
from streetpano.synthetic import CameraSpec, default_scene, write_dataset

CAMERA = CameraSpec(width_px=960, height_px=540)
FLIP_Y = np.diag([1.0, -1.0, 1.0])
MIRROR = np.diag([1.0, 1.0, -1.0])


@pytest.fixture(scope="module")
def street(tmp_path_factory: pytest.TempPathFactory) -> Path:
    path = tmp_path_factory.mktemp("street") / "data"
    scene = default_scene(
        length_m=16, seed=11, camera=CAMERA, path_wobble_m=0.02, exposure_jitter=0.02
    )
    write_dataset(scene, path)
    return path


@pytest.fixture(scope="module")
def estimate(street: Path, tmp_path_factory: pytest.TempPathFactory) -> PoseEstimate:
    return estimate_poses(
        street / "photos", tmp_path_factory.mktemp("work"), CAMERA.focal_px
    )


@pytest.fixture(scope="module")
def report(estimate: PoseEstimate, street: Path) -> AccuracyReport:
    description = json.loads((street / "scene.json").read_text())
    return compare_with_truth(estimate, description)


class TestAccuracyOnSyntheticStreet:
    """Quality limits. Failing one means that camera positions got worse."""

    def test_every_photo_is_registered(self, estimate: PoseEstimate) -> None:
        assert estimate.missing == ()
        assert len(estimate.poses) == 17

    def test_rotation_is_accurate(self, report: AccuracyReport) -> None:
        assert report.rotation_median_deg < 0.1
        assert report.rotation_max_deg < 0.3

    def test_positions_are_accurate(self, report: AccuracyReport) -> None:
        assert report.position_median_m < 0.03
        assert report.position_max_m < 0.1

    def test_facade_distance_is_recovered(self, report: AccuracyReport) -> None:
        # This fails if COLMAP may adjust the focal length (see decision record 0008).
        assert report.facade_distance_m == pytest.approx(15, abs=0.15)

    def test_reprojection_error_is_low(self, estimate: PoseEstimate) -> None:
        assert estimate.mean_reprojection_error_px < 1


def perfect_estimate(
    description: dict[str, Any], scale: float = 1.0, drop: int = 0
) -> PoseEstimate:
    """Build the exact answer in COLMAP's mirrored world, at another scale."""
    camera_to_world = np.array(description["rotation_camera_to_world"])
    rotation = FLIP_Y @ camera_to_world.T @ MIRROR.T
    positions = np.array(description["camera_positions_m"])[drop:]
    poses = tuple(
        CameraPose(
            name=f"{index + drop:05d}.png",
            center=tuple(scale * (MIRROR @ position)),
            rotation_world_to_camera=tuple(tuple(row) for row in rotation),
        )
        for index, position in enumerate(positions)
    )
    points = scale * (MIRROR @ np.array([[x, 2.0, 15.0] for x in range(50)]).T).T
    return PoseEstimate(poses, points, 800.0, (), 0.1)


class TestCompareWithTruth:
    def description(self, **options: Any) -> dict[str, Any]:
        from streetpano.synthetic import _rotation, camera_position

        scene = default_scene(length_m=10, seed=1, camera=CAMERA, **options)
        return {
            "rotation_camera_to_world": _rotation(scene).tolist(),
            "camera_positions_m": [
                camera_position(scene, index) for index in range(scene.photo_count)
            ],
        }

    @pytest.mark.parametrize("scale", [1.0, 0.1, 7.5])
    def test_perfect_estimate_has_no_error_at_any_scale(self, scale: float) -> None:
        result = compare_with_truth(
            perfect_estimate(self.description(), scale), self.description()
        )
        assert result.rotation_max_deg == pytest.approx(0, abs=1e-3)
        assert result.position_max_m == pytest.approx(0, abs=1e-9)
        assert result.facade_distance_m == pytest.approx(15, abs=1e-6)

    def test_tilted_camera_is_supported(self) -> None:
        description = self.description(pitch_deg=8.0, roll_deg=-3.0)
        result = compare_with_truth(perfect_estimate(description), description)
        assert result.rotation_max_deg == pytest.approx(0, abs=1e-3)

    def test_position_error_is_detected(self) -> None:
        description = self.description()
        estimate = perfect_estimate(description)
        moved = estimate.poses[5]
        x, y, z = moved.center
        poses = list(estimate.poses)
        poses[5] = CameraPose(
            moved.name, (x, y + 1.0, z), moved.rotation_world_to_camera
        )
        result = compare_with_truth(
            PoseEstimate(tuple(poses), estimate.points, 800.0, (), 0.1), description
        )
        assert result.position_max_m > 0.5

    def test_rotation_error_is_detected(self) -> None:
        description = self.description()
        estimate = perfect_estimate(description)
        tilted = estimate.poses[5]
        angle = np.radians(10)
        turn = np.array(
            [
                [np.cos(angle), -np.sin(angle), 0],
                [np.sin(angle), np.cos(angle), 0],
                [0, 0, 1],
            ]
        )
        rotation = turn @ np.array(tilted.rotation_world_to_camera)
        poses = list(estimate.poses)
        poses[5] = CameraPose(
            tilted.name, tilted.center, tuple(tuple(row) for row in rotation)
        )
        result = compare_with_truth(
            PoseEstimate(tuple(poses), estimate.points, 800.0, (), 0.1), description
        )
        assert result.rotation_max_deg > 9

    def test_partial_reconstruction_uses_matching_photos(self) -> None:
        description = self.description()
        result = compare_with_truth(perfect_estimate(description, drop=3), description)
        assert result.registered == 8
        assert result.total == 11
        assert result.position_max_m == pytest.approx(0, abs=1e-9)

    def test_empty_estimate_is_rejected(self) -> None:
        empty = PoseEstimate((), np.empty((0, 3)), 800.0, (), 0.0)
        with pytest.raises(ValueError, match="no poses"):
            compare_with_truth(empty, self.description())

    def test_no_points_gives_no_facade_distance(self) -> None:
        description = self.description()
        estimate = perfect_estimate(description)
        without_points = PoseEstimate(estimate.poses, np.empty((0, 3)), 800.0, (), 0.1)
        result = compare_with_truth(without_points, description)
        assert np.isnan(result.facade_distance_m)

    def test_points_at_one_depth_are_handled(self) -> None:
        description = self.description()
        estimate = perfect_estimate(description)
        one_point = PoseEstimate(estimate.poses, estimate.points[:1], 800.0, (), 0.1)
        result = compare_with_truth(one_point, description)
        assert result.facade_distance_m == pytest.approx(15)

    def test_single_camera_has_no_position_spread(self) -> None:
        description = self.description()
        estimate = perfect_estimate(description)
        single = PoseEstimate(estimate.poses[:1], estimate.points, 800.0, (), 0.1)
        assert compare_with_truth(single, description).position_max_m == pytest.approx(
            0
        )


class TestInputChecks:
    def test_missing_folder_is_rejected(self, tmp_path: Path) -> None:
        with pytest.raises(ValueError, match="not found"):
            list_photos(tmp_path / "nothing")

    def test_too_few_photos_are_rejected(self, tmp_path: Path) -> None:
        (tmp_path / "one.jpg").touch()
        (tmp_path / "notes.txt").touch()
        with pytest.raises(ValueError, match="at least 2 photos"):
            list_photos(tmp_path)

    def test_photos_are_sorted_and_filtered(self, tmp_path: Path) -> None:
        for name in ("b.JPG", "a.png", "notes.txt"):
            (tmp_path / name).touch()
        assert [path.name for path in list_photos(tmp_path)] == ["a.png", "b.JPG"]

    def test_invalid_focal_length_is_rejected(self, tmp_path: Path) -> None:
        with pytest.raises(ValueError, match="must be positive"):
            estimate_poses(tmp_path, tmp_path / "work", 0)

    def test_used_work_folder_is_rejected(self, street: Path, tmp_path: Path) -> None:
        (tmp_path / "existing.txt").write_text("keep me")
        with pytest.raises(FileExistsError, match="not empty"):
            estimate_poses(street / "photos", tmp_path, 800)
        assert (tmp_path / "existing.txt").read_text() == "keep me"

    def test_unrelated_photos_raise_a_clear_error(self, tmp_path: Path) -> None:
        photos = tmp_path / "photos"
        photos.mkdir()
        rng = np.random.default_rng(0)
        for index in range(3):
            noise = rng.integers(0, 255, (120, 160, 3), dtype=np.uint8)
            Image.fromarray(noise).save(photos / f"{index}.png")
        with pytest.raises(PoseEstimationError, match="couldn't connect"):
            estimate_poses(photos, tmp_path / "work", 150)


def test_write_estimate(tmp_path: Path) -> None:
    pose = CameraPose("a.png", (1.0, 2.0, 3.0), ((1.0, 0.0, 0.0),) * 3)
    estimate = PoseEstimate((pose,), np.empty((0, 3)), 800.0, ("b.png",), 0.2)
    path = tmp_path / "poses.json"
    write_estimate(estimate, path)
    content = json.loads(path.read_text())
    assert content["format_version"] == 2
    assert content["points"] == []
    assert content["missing"] == ["b.png"]
    assert content["poses"][0]["center"] == [1.0, 2.0, 3.0]


def test_written_points_are_limited(tmp_path: Path) -> None:
    pose = CameraPose("a.png", (1.0, 2.0, 3.0), ((1.0, 0.0, 0.0),) * 3)
    points = np.zeros((50000, 3))
    path = tmp_path / "poses.json"
    write_estimate(PoseEstimate((pose,), points, 800.0, (), 0.2), path)
    assert 10000 < len(json.loads(path.read_text())["points"]) <= 20000


def save_photo(path: Path, orientation: int = 1, focal_35mm: int | None = None) -> Path:
    exif = Image.Exif()
    exif[0x0112] = orientation
    if focal_35mm is not None:
        exif.get_ifd(0x8769)[0xA405] = focal_35mm
    Image.new("RGB", (400, 300)).save(path, exif=exif)
    return path


@pytest.mark.parametrize(
    ("orientation", "expected"),
    [
        (1, (0.0, -1.0, 0.0)),
        (3, (0.0, 1.0, 0.0)),
        (6, (-1.0, 0.0, 0.0)),
        (8, (1.0, 0.0, 0.0)),
        (2, (0.0, -1.0, 0.0)),
    ],
)
def test_up_comes_from_the_orientation_tag(
    tmp_path: Path, orientation: int, expected: tuple[float, float, float]
) -> None:
    assert up_in_camera(save_photo(tmp_path / "a.jpg", orientation)) == expected


def test_focal_length_comes_from_metadata(tmp_path: Path) -> None:
    photo = save_photo(tmp_path / "a.jpg", focal_35mm=26)
    # The diagonal of 400 x 300 is 500 px.
    assert focal_from_metadata(photo) == pytest.approx(26 * 500 / 43.27)


def test_missing_focal_length_gives_a_clear_error(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="--focal-px"):
        focal_from_metadata(save_photo(tmp_path / "a.jpg"))
