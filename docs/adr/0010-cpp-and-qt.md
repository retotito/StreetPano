# 0010: C++ and Qt for the application

**Status:** Accepted. This replaces [0001](0001-python-first-rust-for-slow-parts.md)
and [0009](0009-web-ui-stack.md).

## Context

The goal is a professional, high-performance desktop tool. Earlier decisions chose a
Python pipeline with a local web interface, which was quick to build but would need a
browser, a local server and a Python installation for every user.

We compared C++ and C# using the same code for three typical tasks, on one core of an
Apple M3 with a 12 MP image:

| Task | C++ | C# (.NET 10) |
|---|---:|---:|
| Warping a photo | 0.060 s | 0.118 s |
| Blurring, plain code | 0.013 s | 0.069 s |
| Blurring, C# with hand-written SIMD | 0.013 s | 0.032 s |
| Simple graph cut | 28.7 s | 33.4 s |

This was a small test on one computer, so only the rough size of the gaps matters.

## Decision

Build the application in C++20 with Qt 6 Widgets. Link Qt dynamically under its LGPL
terms, and use only modules available under the LGPL.

Python stays as a developer tool for synthetic streets and accuracy measurement. People
using the app don't need it.

## Alternatives considered

| Option | Why not |
|---|---|
| C# with Avalonia | Safer and quicker to build, but slower in tight loops, with less exact memory control and indirect access to the main libraries. |
| Rust | Excellent safety and speed, but weaker tools for building screens, and the main libraries would still need bridging. |
| Web app | See [0009](0009-web-ui-stack.md). It adds a browser and a server, and uses more memory. |

## Consequences

- COLMAP, OpenCV, libvips, LibRaw and ONNX Runtime can be used directly.
- Memory is freed at predictable times, which helps on computers with 8 GB of RAM.
- Memory errors are possible, and nobody reviews the code by hand every time. These
  safeguards are therefore mandatory, and CI enforces them:
    - No plain `new`, `delete` or C allocation, except in `qt_ownership.hpp`.
    - Threads share data only through queues guarded by standard mutexes, so that
      ThreadSanitizer can check every handoff.
    - AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer run on every change.
    - Compiler warnings and clang-tidy findings fail the build.
    - Damaged files are part of the tests.
- These safeguards reduce memory errors, but they can't guarantee there are none. They
  only check code that the tests run.
- Some AI models may not convert to ONNX. An optional Python helper process remains
  the fallback for those models.
- Development is slower than in Python, so experiments can still be done in `tools/`
  before they're written in C++.
