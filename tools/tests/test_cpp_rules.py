from pathlib import Path

import pytest

from streetpano.cpp_rules import find_violations, main, source_files

SOURCE = Path("src/core/example.cpp")


@pytest.mark.parametrize(
    "code",
    [
        "auto* item = new Item();",
        "delete item;",
        "delete[] items;",
        "auto* data = malloc(10);",
        "free(data);",
    ],
)
def test_forbidden_code_is_reported(code: str) -> None:
    assert len(find_violations(SOURCE, code)) == 1


@pytest.mark.parametrize(
    "code",
    [
        "Item(const Item&) = delete;",
        "// new and delete are fine in a comment",
        "/* free(data) */",
        'auto text = "delete this";',
        "auto renewed = newest + freedom;",
        "auto item = std::make_unique<Item>();",
    ],
)
def test_allowed_code_is_not_reported(code: str) -> None:
    assert find_violations(SOURCE, code) == []


def test_ownership_helper_may_use_new() -> None:
    assert find_violations(Path("src/ui/qt_ownership.hpp"), "new T();") == []


def test_line_numbers_survive_multi_line_comments() -> None:
    code = "/* one\n two */\nint a;\ndelete b;\n"
    assert find_violations(SOURCE, code)[0].line == 4


def test_message_includes_the_location() -> None:
    assert "example.cpp:1:" in str(find_violations(SOURCE, "delete b;")[0])


def repository(tmp_path: Path, code: str) -> Path:
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "a.cpp").write_text(code)
    (tmp_path / "src" / "notes.txt").write_text("delete")
    return tmp_path


def test_only_cpp_files_are_checked(tmp_path: Path) -> None:
    root = repository(tmp_path, "int a;")
    assert [path.name for path in source_files(root, ["src"])] == ["a.cpp"]


def test_main_fails_on_a_violation(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    assert main([str(repository(tmp_path, "delete b;"))]) == 1
    assert "1 problems" in capsys.readouterr().out


def test_main_passes_on_clean_code(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    root = repository(tmp_path, "int a;")
    assert main([str(root)]) == 0
    monkeypatch.chdir(root)
    monkeypatch.setattr("sys.argv", ["cpp_rules"])
    assert main() == 0
