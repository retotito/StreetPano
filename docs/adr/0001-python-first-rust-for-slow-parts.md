# 0001: Python first, Rust for slow parts

**Status:** Accepted

## Context

The project combines AI models, computer vision and large-image processing. Its
methods are unproven, so being able to change them quickly matters most at first.

## Decision

Write the pipeline in Python. Move parts to Rust through PyO3 only when profiling
shows a real need. Use C++ only to extend an existing C++ library directly.

## Consequences

- Direct access to PyTorch, OpenCV and most research code.
- Slow pure-Python loops have to be avoided from the start, using NumPy and pyvips.
- Distribution as a desktop app will take more work.
- C# was rejected, because most vision and AI tooling is built for Python and C++.
