# 0007: Download AI models on demand through a model manager

**Status:** Accepted

## Context

Models can be large, and several are optional. Some have non-commercial terms, and
some won't run well on a computer with 8 GB of RAM. The classic pipeline needs no AI
at all. Downloading everything on the start screen would waste disk space, delay the
first use, and ask users to accept licences for models they may never need.

## Decision

The app works without any models. When a feature first needs one, the app offers to
download it at that moment. A model manager screen and a matching command line show
everything that's installed or available.

## Consequences

- The app starts quickly, and users download only what they use.
- Each feature needs a clear state for when its model isn't installed.
- The project maintains a list of models, with pinned versions and checksums.
- Projects record which model versions they used, so results can be reproduced.
