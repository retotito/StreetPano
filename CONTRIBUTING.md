# Contributing to StreetPano

Thanks for your interest. StreetPano is at an early stage, so it's a good idea to
open an issue to discuss larger changes before you start.

## Setup

You need [uv](https://docs.astral.sh/uv/).

```sh
git clone https://github.com/retotito/StreetPano.git
cd StreetPano
uv sync
uv run pre-commit install
```

## Checks

These must pass before a pull request can be merged:

```sh
uv run pre-commit run --all-files   # formatting, lint and type checks
uv run pytest                       # tests, with at least 95% coverage
uv run mkdocs build --strict        # documentation
```

Preview the documentation with `uv run mkdocs serve`.

## Pull requests

- Keep each pull request to one topic.
- Add tests for new behaviour, and a failing test for each bug you fix.
- Update the documentation and [changelog](CHANGELOG.md) when behaviour changes.
- If you change a figure quoted in the docs, update
  `tests/test_documented_figures.py` as well.
- Record significant design decisions in [decision records](docs/adr/index.md).

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/), for example
`feat: add seam cost for foreground masks`.

## Dependencies and data

- Check the licence before adding a dependency. See
  [AI models](docs/concept/ai-models.md).
- Don't make a non-commercial model or GPL library a required dependency.
- Never commit photos or model weights.

## Licence

By contributing, you agree that your contributions are licensed under the
[Apache License 2.0](LICENSE).
