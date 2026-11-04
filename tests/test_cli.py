import runpy

import pytest

from streetpano import __version__
from streetpano.cli import main


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
