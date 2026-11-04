"""Keep the README and the diagrams consistent with the repository."""

import re
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent
SVG_FILES = sorted((ROOT / "docs" / "assets").glob("*.svg"))
LINK = re.compile(r"\]\((?!https?://|#)([^)#]+)")


@pytest.mark.parametrize("name", ["README.md", "CONTRIBUTING.md"])
def test_relative_links_point_to_existing_files(name: str) -> None:
    text = (ROOT / name).read_text(encoding="utf-8")
    missing = [link for link in LINK.findall(text) if not (ROOT / link).exists()]
    assert not missing


def test_diagrams_exist() -> None:
    assert SVG_FILES


@pytest.mark.parametrize("path", SVG_FILES, ids=lambda path: path.name)
def test_diagrams_are_accessible(path: Path) -> None:
    root = ET.parse(path).getroot()
    namespace = "{http://www.w3.org/2000/svg}"
    assert root.findtext(f"{namespace}title")
    assert root.findtext(f"{namespace}desc")
