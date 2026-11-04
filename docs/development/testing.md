# Testing

## What runs automatically

Every push and pull request runs these checks on Linux and macOS, with Python 3.12
and 3.13:

| Check | Tool |
|---|---|
| Formatting and lint | Ruff |
| Strict type checks | mypy |
| Tests, with at least 95% coverage | pytest, pytest-cov, Hypothesis |
| Documentation builds with no broken links | MkDocs in strict mode |

Run them locally:

```sh
uv run pre-commit run --all-files
uv run pytest
uv run mkdocs build --strict
```

## Test levels

| Level | Purpose | Status |
|---|---|---|
| **Unit** | Individual calculations | In use |
| **Property** | Rules that must always hold, such as "closer objects shift more" | In use |
| **Documented figures** | The numbers quoted in these docs match the code | In use |
| **Synthetic street** | Checks against known geometry | Generator in use, end-to-end checks planned |
| **Memory cap** | A long street stays within its memory budget | Planned |
| **Visual regression** | Output compared with approved reference images | Planned |
| **Real street** | Quality checks on real captures, run manually | Planned |

## Synthetic streets

A test scene with known camera positions, facade planes and foreground objects
gives exact ground truth, needs no large downloads and can run in CI. To create one:

```sh
uv run streetpano synth my-street --length 30 --spacing 1 --seed 7
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
uv run streetpano poses my-street/photos my-street-poses \
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
