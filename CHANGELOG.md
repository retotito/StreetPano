# Changelog

All notable changes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project follows
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed

- The app is now written in C++ with Qt, and the Python code became developer tools in
  `tools/`

### Added

- Marking stays on until Esc, so several objects can be outlined in a row, and on a trackpad
  two fingers move the view and pinching zooms
- An edits panel that lists every edit, can switch one off and changes their order, and
  outlines that disappear once an edit is finished
- Mark an area on the panorama, then remove or keep what's in it from a menu, with undo and
  redo. Edits are saved as decisions, and your photos are never changed
- Saving no longer risks a damaged panorama, and finding camera positions keeps your panorama
  and edits
- Seams that go around objects, and brightness matching between photos
- Straighter facades: the wall is fitted as lines, and adjusted until neighbouring photos
  agree at each seam
- A facade surface that follows the buildings along the street and is laid out as a
  straight panorama, replacing the single facade plane
- Camera positions on real streets: each result is checked as a walk, split where it
  breaks, and joined again using neighbouring photos
- First stitching: `scripts/stitch.sh` takes a folder of photos through to a panorama,
  with straight seams, shown in the app
- Focal length read from photo metadata, and support for phones held any way round
- First window, which opens a folder of photos and shows a film strip and a zoomable
  photo view
- Memory and thread checks in CI, on Linux, macOS and Windows builds
- Concept documentation, diagrams and decision records
- `streetpano estimate` command for capture planning
- `streetpano synth` command for test streets with exact ground truth
- Automated tests, type checks, linting and CI
- Design for downloading AI models on demand
- Design for the user interface and workflow
- `streetpano poses` command, which finds camera positions with COLMAP and can measure
  them against a synthetic street
