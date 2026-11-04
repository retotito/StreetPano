# StreetPano

[![CI](https://github.com/retotito/StreetPano/actions/workflows/ci.yml/badge.svg)](https://github.com/retotito/StreetPano/actions/workflows/ci.yml)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

Open-source software for building very long street panoramas from photos taken
while walking along a street.

> **Status: concept stage.** The design is documented, and the repository has
> tests and CI, but the stitching pipeline does not exist yet. So far, only the
> `streetpano estimate` planning command, the `streetpano synth` test-street generator
> and `streetpano poses` for finding camera positions work.

![Seams chosen by graph cut keep a car whole, while straight seams break it](docs/assets/seams.svg)

## The problem

Walking along a street means every photo is taken from a different position.
The facade lines up, but anything in front of it, such as cars, trees and
people, shifts between photos. Ordinary panorama software assumes one viewpoint,
so these objects get cut or doubled.

## The approach

1. **Geometry does the aligning.** Camera positions are recovered from the
   photos, and each photo is projected onto the facade.
2. **Seams go around problems.** A graph cut chooses which photo supplies each
   pixel, so seams avoid foreground objects.
3. **You stay in control.** The software suggests objects. You accept them or
   click to add more, then choose to keep or remove each one.
4. **AI assists, and generated pixels are tracked.** Models find objects, and
   generative fill is used only for areas that no photo shows. Those pixels are
   recorded in a mask.
5. **Editing is non-destructive.** A project stores the original photos plus your
   decisions, so verticals, seams and slope settings can all be changed later.

## Documentation

| Topic | Page |
|---|---|
| Overview and goals | [Concept overview](docs/concept/overview.md) |
| How to shoot a street | [Capture guide](docs/concept/capture.md) |
| Processing steps | [Pipeline](docs/concept/pipeline.md) |
| Screens and workflow | [User interface](docs/concept/ui-workflow.md) |
| Marking objects | [Foreground objects](docs/concept/foreground.md) |
| Verticals and sloped streets | [Geometry correction](docs/concept/geometry-correction.md) |
| RAW, JPEG and output files | [File formats](docs/concept/formats.md) |
| Working on small computers | [Memory and disk](docs/concept/memory.md) |
| Models and licences | [AI models](docs/concept/ai-models.md) |
| Code structure | [Architecture](docs/development/architecture.md) |
| What gets built when | [Roadmap](docs/roadmap.md) |
| Why decisions were made | [Decision records](docs/adr/index.md) |

## Quick start

You need [uv](https://docs.astral.sh/uv/).

```sh
git clone https://github.com/retotito/StreetPano.git
cd StreetPano
uv sync
uv run streetpano estimate --length 500 --spacing 1
```

```text
Photos:          500
Facade detail:   3.75 mm/px
Panorama:        133333 x 4000 px
Uncompressed:    1.6 GB (8-bit), 3.2 GB (16-bit)
Foreground shift: 533 px between neighbouring photos
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Run all checks with:

```sh
uv run pre-commit run --all-files
uv run pytest
uv run mkdocs build --strict
```

## Licence

StreetPano's code is licensed under the [Apache License 2.0](LICENSE). AI models
are separate downloads with their own terms, and some are limited to
non-commercial use. See [AI models](docs/concept/ai-models.md).
