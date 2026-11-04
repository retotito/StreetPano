import json
from pathlib import Path

import numpy as np
import pytest
from hypothesis import given, settings
from hypothesis import strategies as st
from numpy.typing import NDArray
from PIL import Image

from streetpano.geometry import hidden_facade_height_m, parallax_shift_px
from streetpano.synthetic import (
    FORMAT_VERSION,
    LABEL_BUILDING_BASE,
    LABEL_GROUND,
    LABEL_OBJECT_BASE,
    LABEL_SKY,
    BoxObject,
    Building,
    CameraSpec,
    StreetScene,
    camera_position,
    default_scene,
    iter_frames,
    project_point,
    render_frame,
    write_dataset,
)

SMALL = CameraSpec(width_px=320, height_px=180)
WIDE = CameraSpec(width_px=1280, height_px=720)
GREY = (0.6, 0.6, 0.6)


def two_buildings(edge_m: float = 0.5) -> tuple[Building, ...]:
    return (
        Building(-500, edge_m, 40, GREY),
        Building(edge_m, 500, 40, (0.8, 0.7, 0.5)),
    )


def scene_with(*objects: BoxObject, **options: object) -> StreetScene:
    settings_ = {"length_m": 2.0, "spacing_m": 1.0, "camera": WIDE, **options}
    return StreetScene(buildings=two_buildings(), objects=objects, **settings_)  # type: ignore[arg-type]


def building_edge_columns(labels: NDArray[np.uint16]) -> NDArray[np.int64]:
    """Return the column of the boundary between two buildings, for each row."""
    left, right = LABEL_BUILDING_BASE, LABEL_BUILDING_BASE + 1
    rows = (labels[:, :-1] == left) & (labels[:, 1:] == right)
    found_rows, found_columns = np.nonzero(rows)
    assert found_rows.size
    result: NDArray[np.int64] = found_columns.astype(np.int64)
    return result


class TestValidation:
    def test_scene_needs_buildings(self) -> None:
        with pytest.raises(ValueError, match="at least one building"):
            StreetScene(length_m=1, spacing_m=1, buildings=())

    @pytest.mark.parametrize(
        ("field", "value"),
        [("spacing_m", 0), ("facade_distance_m", -1), ("camera_height_m", 0)],
    )
    def test_non_positive_values_are_rejected(self, field: str, value: float) -> None:
        options = {"spacing_m": 1.0, field: value}
        with pytest.raises(ValueError, match="must be positive"):
            StreetScene(length_m=1, buildings=two_buildings(), **options)  # type: ignore[arg-type]

    def test_negative_length_is_rejected(self) -> None:
        with pytest.raises(ValueError, match="must not be negative"):
            StreetScene(length_m=-1, spacing_m=1, buildings=two_buildings())

    def test_invalid_camera_is_rejected(self) -> None:
        with pytest.raises(ValueError, match="camera dimensions"):
            StreetScene(
                length_m=1,
                spacing_m=1,
                buildings=two_buildings(),
                camera=CameraSpec(width_px=0),
            )

    def test_object_behind_facade_is_rejected(self) -> None:
        box = BoxObject("wall", 0, 1, 14, 16, 2, GREY)
        with pytest.raises(ValueError, match="between the path and the facade"):
            scene_with(box)

    def test_photo_index_is_checked(self) -> None:
        with pytest.raises(IndexError, match="outside"):
            camera_position(scene_with(), 99)

    def test_point_behind_camera_is_rejected(self) -> None:
        with pytest.raises(ValueError, match="behind the camera"):
            project_point(scene_with(), 0, (0, 1, -5))


class TestProjection:
    @given(
        u=st.integers(min_value=10, max_value=1270),
        v=st.integers(min_value=10, max_value=710),
    )
    @settings(max_examples=25, deadline=None)
    def test_facade_points_project_to_their_pixel(self, u: int, v: int) -> None:
        scene = scene_with(pitch_deg=4.0, roll_deg=-2.0)
        focal = scene.camera.focal_px
        direction = np.array([u + 0.5 - 640, 360 - v - 0.5, focal]) / focal
        camera_to_world = project_point_rotation(scene)
        world_ray = camera_to_world @ direction
        origin = np.array(camera_position(scene, 1))
        point = origin + world_ray * (15 - origin[2]) / world_ray[2]
        found_u, found_v, _ = project_point(scene, 1, tuple(point))
        assert found_u == pytest.approx(u + 0.5)
        assert found_v == pytest.approx(v + 0.5)

    def test_depth_is_measured_along_the_camera_axis(self) -> None:
        frame = render_frame(scene_with(length_m=0.0), 0)
        is_facade = frame.labels >= LABEL_BUILDING_BASE
        assert np.allclose(frame.depth_m[is_facade], 15, atol=1e-3)

    def test_sky_has_infinite_depth(self) -> None:
        scene = StreetScene(
            length_m=0,
            spacing_m=1,
            buildings=(Building(-500, 500, 2, GREY),),
            camera=SMALL,
        )
        frame = render_frame(scene, 0)
        assert (frame.labels == LABEL_SKY).any()
        assert np.isinf(frame.depth_m[frame.labels == LABEL_SKY]).all()


def project_point_rotation(scene: StreetScene) -> NDArray[np.float64]:
    pitch, roll = np.radians(scene.pitch_deg), np.radians(scene.roll_deg)
    pitch_matrix = np.array(
        [
            [1, 0, 0],
            [0, np.cos(pitch), np.sin(pitch)],
            [0, -np.sin(pitch), np.cos(pitch)],
        ]
    )
    roll_matrix = np.array(
        [
            [np.cos(roll), -np.sin(roll), 0],
            [np.sin(roll), np.cos(roll), 0],
            [0, 0, 1],
        ]
    )
    result: NDArray[np.float64] = pitch_matrix @ roll_matrix
    return result


class TestAgreesWithPlanningGeometry:
    def test_rendered_parallax_matches_the_formula(self) -> None:
        thin_box = BoxObject("sign", 0.6, 1.4, 5.0, 5.01, 3.0, GREY)
        scene = scene_with(thin_box, length_m=1.0)
        first, second = render_frame(scene, 0), render_frame(scene, 1)

        def object_centre(labels: NDArray[np.uint16]) -> float:
            columns = np.nonzero(labels == LABEL_OBJECT_BASE)[1]
            return float(columns.mean())

        object_shift = object_centre(first.labels) - object_centre(second.labels)
        facade_shift = float(
            building_edge_columns(first.labels).mean()
            - building_edge_columns(second.labels).mean()
        )
        expected = parallax_shift_px(scene.camera.focal_px, 1.0, 5.0, 15.0)
        assert object_shift - facade_shift == pytest.approx(expected, abs=1.5)

    @pytest.mark.parametrize("camera_height_m", [1.6, 2.2, 3.0])
    def test_rendered_occlusion_matches_the_formula(
        self, camera_height_m: float
    ) -> None:
        car_row = BoxObject("cars", -500, 500, 5.0, 7.0, 1.5, GREY)
        scene = scene_with(car_row, length_m=0.0, camera_height_m=camera_height_m)
        frame = render_frame(scene, 0)

        rows = np.nonzero(frame.labels[:, 640] >= LABEL_BUILDING_BASE)[0]
        rows = rows[frame.labels[rows, 640] < LABEL_OBJECT_BASE]
        lowest_v = rows.max() + 1.0
        lowest_m = camera_height_m + (360 - lowest_v) * 15 / scene.camera.focal_px

        expected = hidden_facade_height_m(camera_height_m, 1.5, 5.0, 7.0, 15.0)
        assert lowest_m == pytest.approx(expected, abs=0.03)


class TestCaptureConditions:
    def test_level_camera_keeps_building_edges_vertical(self) -> None:
        columns = building_edge_columns(render_frame(scene_with(), 0).labels)
        assert np.ptp(columns) == 0

    def test_tilted_camera_makes_building_edges_lean(self) -> None:
        # An edge at the image centre barely moves, so this one is off to the side.
        scene = StreetScene(
            length_m=0,
            spacing_m=1,
            buildings=two_buildings(edge_m=5.0),
            camera=WIDE,
            pitch_deg=12.0,
        )
        columns = building_edge_columns(render_frame(scene, 0).labels)
        assert np.ptp(columns) > 5

    def test_rolled_camera_makes_building_edges_lean(self) -> None:
        scene = scene_with(roll_deg=3.0)
        columns = building_edge_columns(render_frame(scene, 0).labels)
        assert np.ptp(columns) > 5

    def test_camera_follows_a_sloped_street(self) -> None:
        scene = scene_with(slope=0.05, length_m=20.0)
        assert camera_position(scene, 20)[1] == pytest.approx(1.6 + 20 * 0.05)

    def test_sloped_ground_is_visible_below_the_facade(self) -> None:
        scene = StreetScene(
            length_m=0,
            spacing_m=1,
            slope=0.1,
            buildings=(Building(-500, 500, 40, GREY),),
            camera=WIDE,
        )
        labels = render_frame(scene, 0).labels
        lowest_left = np.nonzero(labels[:, 100] != LABEL_GROUND)[0].max()
        lowest_right = np.nonzero(labels[:, 1180] != LABEL_GROUND)[0].max()
        assert lowest_right < lowest_left

    def test_steep_upward_tilt_sees_no_ground(self) -> None:
        labels = render_frame(scene_with(pitch_deg=45.0), 0).labels
        assert not (labels == LABEL_GROUND).any()

    def test_path_wobble_changes_camera_positions(self) -> None:
        scene = scene_with(path_wobble_m=0.05)
        positions = [camera_position(scene, index) for index in range(3)]
        assert len({position[2] for position in positions}) == 3

    def test_exposure_jitter_changes_brightness_between_photos(self) -> None:
        still = scene_with(length_m=0.0)
        jittered = scene_with(length_m=0.0, exposure_jitter=0.1, seed=3)
        assert not np.array_equal(
            render_frame(still, 0).image, render_frame(jittered, 0).image
        )

    def test_moving_object_changes_position_between_photos(self) -> None:
        person = BoxObject("person", 0, 0.5, 9, 9.4, 1.8, GREY, speed_m_per_photo=1.0)
        scene = scene_with(person, length_m=2.0)
        assert person.is_moving
        centres = []
        for frame in iter_frames(scene):
            columns = np.nonzero(frame.labels == LABEL_OBJECT_BASE)[1]
            centres.append(float(columns.mean()))
        # The person walks with the camera at the same speed, so stays in place.
        assert centres[0] == pytest.approx(centres[2], abs=1.0)

    def test_ray_parallel_to_a_box_face_is_handled(self) -> None:
        # Column 640 has a ray with almost no sideways component, and a level camera
        # has a horizontal row. Neither may produce invalid values.
        box = BoxObject("wall", -1, 1, 6, 7, 3, GREY)
        frame = render_frame(scene_with(box), 0)
        assert (frame.labels == LABEL_OBJECT_BASE).any()
        assert not np.isnan(frame.depth_m).any()


class TestDefaultScene:
    def test_has_the_expected_photo_count(self) -> None:
        assert default_scene(length_m=10, spacing_m=2).photo_count == 6

    def test_contains_every_kind_of_content(self) -> None:
        scene = default_scene(camera=SMALL)
        assert {box.name.split("-")[0] for box in scene.objects} == {
            "car",
            "tree",
            "person",
        }
        assert any(box.is_moving for box in scene.objects)
        labels = render_frame(scene, 15).labels
        assert (labels >= LABEL_OBJECT_BASE).any()
        assert ((labels >= LABEL_BUILDING_BASE) & (labels < LABEL_OBJECT_BASE)).any()

    @pytest.mark.parametrize("seed", range(5))
    def test_trees_are_narrow_enough_to_see_past(self, seed: int) -> None:
        trees = [
            box
            for box in default_scene(seed=seed).objects
            if box.name.startswith("tree")
        ]
        assert trees
        assert all(0.4 <= box.x_end_m - box.x_start_m <= 1.0 for box in trees)

    def test_same_seed_renders_identical_photos(self) -> None:
        first = render_frame(default_scene(seed=4, camera=SMALL), 3)
        second = render_frame(default_scene(seed=4, camera=SMALL), 3)
        assert np.array_equal(first.image, second.image)
        assert np.array_equal(first.labels, second.labels)

    def test_different_seeds_render_different_photos(self) -> None:
        first = render_frame(default_scene(seed=1, camera=SMALL), 3)
        second = render_frame(default_scene(seed=2, camera=SMALL), 3)
        assert not np.array_equal(first.image, second.image)

    def test_facade_texture_stays_fixed_to_the_facade(self) -> None:
        scene = StreetScene(
            length_m=2,
            spacing_m=1,
            buildings=(Building(-500, 500, 40, GREY),),
            camera=WIDE,
            exposure_jitter=0,
        )
        first, second = render_frame(scene, 0).image, render_frame(scene, 1).image
        shift = round(scene.camera.focal_px / 15)
        difference = np.abs(
            first[100:300, 600 + shift : 800 + shift].astype(int)
            - second[100:300, 600:800].astype(int)
        )
        assert difference.mean() < 12

    def test_frames_are_generated_lazily(self) -> None:
        frames = iter_frames(default_scene(length_m=1000, camera=SMALL))
        assert next(frames).index == 0


class TestWriteDataset:
    def test_writes_photos_truth_and_description(self, tmp_path: Path) -> None:
        scene = default_scene(length_m=2, camera=SMALL, seed=5)
        write_dataset(scene, tmp_path / "street")

        photos = sorted((tmp_path / "street" / "photos").iterdir())
        assert [photo.name for photo in photos] == [
            "00000.png",
            "00001.png",
            "00002.png",
        ]
        with Image.open(photos[1]) as image:
            assert image.size == (320, 180)

        with np.load(tmp_path / "street" / "truth" / "00001.npz") as truth:
            assert truth["labels"].shape == (180, 320)
            assert truth["depth_m"].shape == (180, 320)

        description = json.loads((tmp_path / "street" / "scene.json").read_text())
        assert description["format_version"] == FORMAT_VERSION
        assert len(description["camera_positions_m"]) == 3
        assert description["scene"]["seed"] == 5

    def test_refuses_a_folder_that_is_not_empty(self, tmp_path: Path) -> None:
        (tmp_path / "existing.txt").write_text("keep me")
        with pytest.raises(FileExistsError, match="not empty"):
            write_dataset(default_scene(length_m=1, camera=SMALL), tmp_path)
        assert (tmp_path / "existing.txt").read_text() == "keep me"

    def test_accepts_an_empty_folder(self, tmp_path: Path) -> None:
        write_dataset(default_scene(length_m=0, camera=SMALL), tmp_path)
        assert (tmp_path / "scene.json").exists()
