# StreetPano developer tools

Python tools for developing StreetPano. People using the app don't need them.

- `streetpano synth` renders synthetic streets with exact ground truth.
- `streetpano poses` finds camera positions with COLMAP and measures their accuracy.
- `streetpano estimate` calculates photo counts and output sizes.
- `python -m streetpano.cpp_rules` enforces the project's C++ memory rules.

Run them from the repository root with `uv run --project tools`.
