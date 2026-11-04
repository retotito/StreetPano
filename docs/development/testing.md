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
| **Synthetic street** | End-to-end checks against known geometry | Planned |
| **Memory cap** | A long street stays within its memory budget | Planned |
| **Visual regression** | Output compared with approved reference images | Planned |
| **Real street** | Quality checks on real captures, run manually | Planned |

## Synthetic streets

A test scene with known camera positions, facade planes and foreground objects
gives exact ground truth, needs no large downloads and can run in CI. It allows
direct measurements:

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
