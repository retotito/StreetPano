# Contributing to StreetPano

Thanks for your interest. StreetPano is at an early stage, so it's a good idea to
open an issue to discuss larger changes before you start.

## Setup

You need CMake, Ninja, Qt 6 and [uv](https://docs.astral.sh/uv/). On macOS:

```sh
brew install cmake ninja qtbase
git clone https://github.com/retotito/StreetPano.git
cd StreetPano
uv sync --project tools
uv run --project tools pre-commit install
./scripts/dev.sh
```

## Checks

These must pass before a pull request can be merged:

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
uv run --project tools pre-commit run --all-files
(cd tools && uv run pytest)
uv run --project tools mkdocs build --strict
```

Preview the documentation with `uv run --project tools mkdocs serve`.

## C++ rules

Read the [memory and thread rules](docs/development/architecture.md) before changing C++
code. In short:

- Never use plain `new`, `delete` or C allocation. CI rejects them.
- Give anything that holds images a memory budget and a test for it.
- Share data between threads only through mutex-guarded queues.
- Don't suppress a sanitizer finding unless it's inside a system library.

## Pull requests

- Keep each pull request to one topic.
- Add tests for new behaviour, and a failing test for each bug you fix.
- Update the documentation and [changelog](CHANGELOG.md) when behaviour changes.
- If you change a figure quoted in the docs, update
  `tools/tests/test_documented_figures.py` as well.
- Record significant design decisions in [decision records](docs/adr/index.md).

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/), for example
`feat: add seam cost for foreground masks`.

## Dependencies and data

- Check the licence before adding a dependency. See
  [AI models](docs/concept/ai-models.md).
- Don't make a non-commercial model or GPL library a required dependency.
- Use only Qt modules available under the LGPL.
- Never commit photos or model weights.

## Licence

By contributing, you agree that your contributions are licensed under the
[Apache License 2.0](LICENSE).
