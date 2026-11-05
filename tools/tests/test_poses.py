import json
from pathlib import Path
from typing import Any, ClassVar

import numpy as np
import pytest
from PIL import Image

import streetpano.poses as poses_module
from streetpano.poses import (
    AccuracyReport,
    CameraPose,
    PoseEstimate,
    PoseEstimationError,
    Section,
    compare_with_truth,
    estimate_poses,
    focal_from_metadata,
    join_sections,
    list_photos,
    path_problems,
    section_problems,
    select_sections,
    split_points,
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


def straight_walk(count: int = 10, step: float = 1.0) -> tuple[Any, Any]:
    centers = np.array([[index * step, 0.0, 0.0] for index in range(count)])
    forwards = np.tile([0.0, 0.0, 1.0], (count, 1))
    return centers, forwards


def turned_by(degrees: float) -> list[float]:
    angle = np.radians(degrees)
    return [float(np.sin(angle)), 0.0, float(np.cos(angle))]


class TestPathChecks:
    def test_a_steady_walk_has_no_problems(self) -> None:
        centers, forwards = straight_walk()
        assert path_problems(centers, forwards) == []
        assert split_points(centers, forwards) == []

    def test_too_few_photos_cant_be_judged(self) -> None:
        centers, forwards = straight_walk(2)
        assert path_problems(centers, forwards) == []
        assert split_points(centers, forwards) == []

    def test_cameras_that_dont_move_are_a_problem(self) -> None:
        centers, forwards = straight_walk(step=0.0)
        assert path_problems(centers, forwards) == ["the cameras don't move"]

    def test_a_jump_is_a_problem_and_a_place_to_split(self) -> None:
        centers, forwards = straight_walk()
        centers[5:] += [20.0, 0.0, 0.0]
        assert "21 times" in path_problems(centers, forwards)[0]
        assert split_points(centers, forwards) == [4]

    def test_a_very_short_step_is_a_place_to_split(self) -> None:
        centers, forwards = straight_walk()
        centers[5:] -= [0.8, 0.0, 0.0]
        assert split_points(centers, forwards) == [4]

    def test_a_sudden_turn_is_a_problem_and_a_place_to_split(self) -> None:
        centers, forwards = straight_walk()
        forwards[5:] = turned_by(40)
        assert any("40 degrees" in item for item in path_problems(centers, forwards))
        assert split_points(centers, forwards) == [4]

    def test_a_slow_turn_along_the_street_is_fine(self) -> None:
        centers, forwards = straight_walk()
        forwards = np.array([turned_by(5.0 * index) for index in range(10)])
        assert path_problems(centers, forwards) == []

    def test_collapsed_cameras_are_a_problem(self) -> None:
        # Measured on a real street: part of the walk shrank to almost one spot.
        steps = [1.0] * 8 + [0.001] * 5
        centers = np.array([[x, 0.0, 0.0] for x in np.cumsum([0.0, *steps])])
        forwards = np.tile([0.0, 0.0, 1.0], (len(centers), 1))
        assert "many cameras are in almost the same place" in path_problems(
            centers, forwards
        )

    def test_a_sudden_change_of_scale_is_a_problem(self) -> None:
        # Measured on a real street: one half came out at about 60% of the other's size.
        steps = [0.33] * 12 + [0.55] * 13
        centers = np.array([[x, 0.0, 0.0] for x in np.cumsum([0.0, *steps])])
        forwards = np.tile([0.0, 0.0, 1.0], (len(centers), 1))
        assert path_problems(centers, forwards) == [
            "the step size changes suddenly along the street"
        ]


def section(
    first: int,
    count: int,
    scale: float = 1.0,
    world: Any = None,
    offset: Any = (0.0, 0.0, 0.0),
) -> Section:
    """A straight walk along X, seen in another world if asked."""
    rotation = np.eye(3) if world is None else np.array(world)
    poses = tuple(
        CameraPose(
            name=f"{first + index:05d}.jpg",
            center=tuple(scale * rotation @ [float(index), 0.0, 0.0] + offset),
            rotation_world_to_camera=tuple(tuple(row) for row in rotation.T),
        )
        for index in range(count)
    )
    points = scale * (rotation @ np.array([[float(count), 1.0, 5.0]]).T).T + offset
    return Section(poses, points, 1.0)


class TestSections:
    ORDER: ClassVar[dict[str, int]] = {f"{index:05d}.jpg": index for index in range(30)}

    def test_largest_sections_win_when_they_share_photos(self) -> None:
        chosen = select_sections(
            [section(0, 4), section(3, 8), section(11, 3)], self.ORDER
        )
        assert [item.poses[0].name for item in chosen] == ["00003.jpg", "00011.jpg"]

    def test_a_bad_section_is_reported(self) -> None:
        bad = section(0, 10)
        centers = [list(pose.center) for pose in bad.poses]
        centers[5][0] += 50
        poses = tuple(
            CameraPose(p.name, (c[0], c[1], c[2]), p.rotation_world_to_camera)
            for p, c in zip(bad.poses, centers, strict=True)
        )
        assert section_problems(Section(poses, bad.points, 1.0))

    def test_joining_undoes_the_second_sections_own_world(self) -> None:
        turn = np.array([[0.0, -1.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]])
        joined = join_sections(
            section(0, 5),
            section(5, 4, scale=0.5, world=turn, offset=np.array([3.0, -2.0, 7.0])),
            np.eye(3),
            np.array([1.0, 0.0, 0.0]),
            1,
        )
        assert joined is not None
        centers = np.array([pose.center for pose in joined.poses])
        assert centers == pytest.approx(
            np.array([[float(index), 0.0, 0.0] for index in range(9)])
        )
        for pose in joined.poses:
            assert np.array(pose.rotation_world_to_camera) == pytest.approx(np.eye(3))
        # The second section's point keeps its place relative to its cameras.
        assert joined.points[-1] == pytest.approx([9.0, 1.0, 5.0])

    def test_a_missing_photo_leaves_room_for_it(self) -> None:
        joined = join_sections(
            section(0, 5), section(6, 4), np.eye(3), np.array([1.0, 0.0, 0.0]), 2
        )
        assert joined is not None
        assert joined.poses[5].center == pytest.approx((6.0, 0.0, 0.0))

    def test_a_join_against_the_walking_direction_is_refused(self) -> None:
        assert (
            join_sections(
                section(0, 5), section(5, 4), np.eye(3), np.array([-1.0, 0.0, 0.0]), 1
            )
            is None
        )

    @pytest.mark.parametrize("sizes", [(1, 4), (4, 1)])
    def test_a_single_photo_has_no_usual_step(self, sizes: tuple[int, int]) -> None:
        assert (
            join_sections(
                section(0, sizes[0]),
                section(5, sizes[1]),
                np.eye(3),
                np.array([1.0, 0.0, 0.0]),
                1,
            )
            is None
        )

    def test_sections_that_dont_move_cant_be_joined(self) -> None:
        assert (
            join_sections(
                section(0, 4),
                section(5, 4, scale=0.0),
                np.eye(3),
                np.array([1.0, 0.0, 0.0]),
                1,
            )
            is None
        )


def split_estimate(estimate: PoseEstimate) -> list[Section]:
    middle = len(estimate.poses) // 2
    first = Section(estimate.poses[:middle], estimate.points, 1.0)
    # The second part gets its own world, as COLMAP gives a separate part.
    turn = np.array([[0.0, -1.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]])
    moved = tuple(
        CameraPose(
            pose.name,
            tuple(0.5 * turn @ np.array(pose.center)),
            tuple(
                tuple(row) for row in np.array(pose.rotation_world_to_camera) @ turn.T
            ),
            pose.up_in_camera,
        )
        for pose in estimate.poses[middle:]
    )
    return [first, Section(moved, np.empty((0, 3)), 1.0)]


@pytest.fixture(scope="module")
def work(street: Path, tmp_path_factory: pytest.TempPathFactory) -> Any:
    import pycolmap

    work = tmp_path_factory.mktemp("connect")
    estimate = estimate_poses(street / "photos", work, CAMERA.focal_px)
    order = {pose.name: index for index, pose in enumerate(estimate.poses)}
    return pycolmap, work / "database.db", estimate, order


class TestConnectingSectionsWithCOLMAP:
    def test_separate_parts_rejoin_into_one_street(self, work: Any) -> None:
        pycolmap, database, estimate, order = work
        joined = poses_module._connect_sections(
            pycolmap, database, split_estimate(estimate), order
        )
        assert joined is not None
        assert [pose.name for pose in joined.poses] == [
            pose.name for pose in estimate.poses
        ]
        assert not section_problems(joined)
        # The path keeps its shape, compared in steps to allow for the assumed scale.
        centers = np.array([pose.center for pose in joined.poses])
        truth = np.array([pose.center for pose in estimate.poses])
        steps = np.linalg.norm(np.diff(centers, axis=0), axis=1)
        true_steps = np.linalg.norm(np.diff(truth, axis=0), axis=1)
        assert steps == pytest.approx(true_steps, rel=0.1)

    def test_nothing_to_connect(self, work: Any) -> None:
        pycolmap, database, _, order = work
        assert poses_module._connect_sections(pycolmap, database, [], order) is None

    def test_too_few_matches_leaves_the_longest_part(
        self, work: Any, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        pycolmap, database, estimate, order = work
        monkeypatch.setattr(poses_module, "_MIN_JOIN_MATCHES", 10**9)
        parts = split_estimate(estimate)
        joined = poses_module._connect_sections(pycolmap, database, parts, order)
        assert joined is not None
        assert len(joined.poses) == max(len(part.poses) for part in parts)

    def test_a_wide_gap_isnt_bridged(self, work: Any) -> None:
        pycolmap, database, estimate, order = work
        first, second = split_estimate(estimate)
        wide = Section(second.poses[3:], second.points, 1.0)
        joined = poses_module._connect_sections(
            pycolmap, database, [first, wide], order
        )
        assert joined is not None
        assert len(joined.poses) == max(len(first.poses), len(wide.poses))

    def test_a_failed_estimate_of_the_gap_leaves_the_parts(
        self, work: Any, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        pycolmap, database, estimate, order = work
        monkeypatch.setattr(pycolmap, "estimate_essential_matrix", lambda *_: None)
        parts = split_estimate(estimate)
        joined = poses_module._connect_sections(pycolmap, database, parts, order)
        assert joined is not None
        assert len(joined.poses) == max(len(part.poses) for part in parts)


class TestEstimateFallbacks:
    def fake_mapping(
        self, monkeypatch: pytest.MonkeyPatch, sections: list[Section]
    ) -> None:
        monkeypatch.setattr(
            poses_module, "_map_sections", lambda *_: sections, raising=True
        )

    def test_no_reconstruction_is_an_error(
        self, street: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        self.fake_mapping(monkeypatch, [])
        with pytest.raises(PoseEstimationError):
            estimate_poses(street / "photos", tmp_path / "work", CAMERA.focal_px)

    def test_an_impossible_path_is_used_with_a_warning(
        self, street: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        photos = list_photos(street / "photos")
        steps = [0.0, 1.0, 1.0, 1.0, 9.0, 1.0, 1.0, 1.0]
        positions = np.cumsum(steps)
        poses = tuple(
            CameraPose(
                photo.name,
                (float(x), 0.0, 0.0),
                ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)),
            )
            for photo, x in zip(photos, positions, strict=False)
        )
        self.fake_mapping(monkeypatch, [Section(poses, np.zeros((1, 3)), 1.0)])
        result = estimate_poses(street / "photos", tmp_path / "work", CAMERA.focal_px)
        assert "looks wrong" in result.warnings[0]
        assert len(result.poses) == len(steps)
