import runpy
from pathlib import Path

import numpy as np
import pytest

from streetpano import __version__
from streetpano.cli import main
from streetpano.poses import CameraPose, PoseEstimate


def test_version(capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit) as exit_info:
        main(["--version"])
    assert exit_info.value.code == 0
    assert __version__ in capsys.readouterr().out


def test_estimate_defaults(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["estimate"]) == 0
    output = capsys.readouterr().out
    assert "Photos:          500" in output
    assert "3.75 mm/px" in output
    assert "133333 x 4000 px" in output
    assert "1.6 GB (8-bit), 3.2 GB (16-bit)" in output
    assert "533 px between" in output


def test_invalid_input_exits_with_an_error(
    capsys: pytest.CaptureFixture[str],
) -> None:
    with pytest.raises(SystemExit) as exit_info:
        main(["estimate", "--spacing", "0"])
    assert exit_info.value.code == 2
    assert "must be positive" in capsys.readouterr().err


def test_a_command_is_required() -> None:
    with pytest.raises(SystemExit) as exit_info:
        main([])
    assert exit_info.value.code == 2


def test_module_entry_point(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.argv", ["streetpano", "estimate"])
    with pytest.raises(SystemExit) as exit_info:
        runpy.run_module("streetpano", run_name="__main__")
    assert exit_info.value.code == 0


def test_synth_writes_a_dataset(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    output = tmp_path / "street"
    arguments = [
        "synth",
        str(output),
        "--length",
        "1",
        "--width",
        "64",
        "--height",
        "36",
    ]
    assert main(arguments) == 0
    assert "Wrote 2 photos" in capsys.readouterr().out
    assert (output / "scene.json").exists()


def test_synth_refuses_a_folder_that_is_not_empty(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    (tmp_path / "existing.txt").write_text("keep me")
    with pytest.raises(SystemExit) as exit_info:
        main(["synth", str(tmp_path)])
    assert exit_info.value.code == 2
    assert "not empty" in capsys.readouterr().err


@pytest.fixture
def small_street(tmp_path: Path) -> Path:
    street = tmp_path / "street"
    main(["synth", str(street), "--length", "8", "--width", "960", "--height", "540"])
    return street


def test_poses_measures_a_synthetic_street(
    small_street: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    capsys.readouterr()
    output = tmp_path / "poses"
    arguments = [
        "poses",
        str(small_street / "photos"),
        str(output),
        "--focal-px",
        "640",
        "--truth",
        str(small_street / "scene.json"),
    ]
    assert main(arguments) == 0
    text = capsys.readouterr().out
    assert "Registered 9 photos" in text
    assert "Facade distance:" in text
    assert (output / "poses.json").exists()


def test_poses_reports_missing_photos(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    pose = CameraPose("a.png", (0.0, 0.0, 0.0), ((1.0, 0.0, 0.0),) * 3)
    result = PoseEstimate((pose,), np.empty((0, 3)), 800.0, ("b.png",), 0.2)
    monkeypatch.setattr("streetpano.cli.estimate_poses", lambda *_: result)
    arguments = ["poses", "photos", str(tmp_path), "--focal-px", "800"]
    assert main(arguments) == 0
    assert "Missing: b.png" in capsys.readouterr().out
