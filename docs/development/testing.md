# Testing

## What runs automatically

Every push and pull request runs these checks:

| Check | Tools | Where |
|---|---|---|
| Build and tests, with warnings as errors | CMake, Catch2, Qt Test | Linux (GCC, Clang), macOS, Windows |
| Memory errors, leaks and undefined behaviour | AddressSanitizer, LeakSanitizer, UndefinedBehaviorSanitizer | Linux |
| Thread errors | ThreadSanitizer | Linux |
| Static analysis | clang-tidy | Linux |
| C++ coverage, at least 85% of lines | gcovr | Linux |
| C++ memory rules and formatting | `cpp_rules`, clang-format | Linux |
| Python tools, with at least 95% coverage | Ruff, mypy, pytest, Hypothesis | Linux, macOS |
| Documentation with no broken links | MkDocs in strict mode | Linux |

Run the main ones locally:

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
uv run --project tools pre-commit run --all-files
uv run --project tools mkdocs build --strict
```

LeakSanitizer doesn't run on Apple Silicon, so leaks are checked in the Linux job.

## Keeping threads checkable

ThreadSanitizer can't see inside Qt's precompiled libraries. When work passed through
Qt's thread pool and event queue, it reported data races that were probably false
alarms. Suppressing them could have hidden real bugs too, so `ImageLoader` uses standard
threads and a standard mutex instead, and Qt only delivers a signal that results are
ready. Follow this pattern for new background work.

On Linux, ThreadSanitizer still reports memory calls made inside Qt's own libraries.
`tests/sanitizers/tsan.supp` ignores only those calls, while accesses from StreetPano's
code stay checked. The Mac run needs no suppressions. Don't suppress anything outside
system libraries.

## Test levels

| Level | Purpose | Status |
|---|---|---|
| **Unit** | Individual calculations and classes | In use |
| **Property** | Rules that must always hold, such as "closer objects shift more" | In use |
| **Documented figures** | The numbers quoted in these docs match the code | In use |
| **Damaged files** | Empty, truncated, random and oversized images, and damaged camera position files, fail safely | In use |
| **Stitching accuracy** | A panorama of a known facade is compared with its true colours | In use |
| **Screen tests** | Real windows without a display, including checks of drawn pixels | In use |
| **Synthetic street** | Checks against known geometry | Generator in use, end-to-end checks planned |
| **Memory cap** | A long street stays within its memory budget | Planned |
| **Visual regression** | Output compared with approved reference images | Planned |
| **Real street** | Quality checks on real captures, run manually | Planned |

## Synthetic streets

A test scene with known camera positions, facade planes and foreground objects
gives exact ground truth, needs no large downloads and can run in CI. To create one:

```sh
uv run --project tools streetpano synth my-street --length 30 --spacing 1 --seed 7
```

This writes a new folder containing:

| Path | Contents |
|---|---|
| `photos/00000.png` | The rendered photos |
| `truth/00000.npz` | Exact `labels` (sky, ground, building or object) and `depth_m` for every pixel |
| `scene.json` | Camera positions, camera model and scene description |

Options such as `--slope`, `--pitch`, `--roll`, `--wobble` and `--exposure-jitter` add
capture problems on purpose. The renderer uses flat facades and box-shaped objects,
without lens distortion, so passing on synthetic streets is necessary but not
sufficient. Real photos are still needed.

The tests confirm that rendered parallax and occlusion match the formulas in
`streetpano.geometry`. They also set accuracy limits for camera positions, so a change
that makes them worse fails the tests. To measure a street yourself:

```sh
uv run --project tools streetpano poses my-street/photos my-street-poses \
  --focal-px 853.333 --truth my-street/scene.json
```

Synthetic streets allow direct measurements:

- Camera position error
- Facade alignment error, in pixels
- Whether any seam crosses a "keep whole" object
- Share of generated pixels
- Peak memory use

## AI models in tests

Fast tests must not download models. Models are replaced with simple stand-ins that
return known masks, and tests with real models run separately on a schedule.

## Test data

Photos and model weights never go in git. Small synthetic scenes are generated when
the tests run.
