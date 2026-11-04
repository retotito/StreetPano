# 0008: Hold focal length fixed when finding camera positions

**Status:** Accepted

## Context

On a flat facade photographed along a nearly straight line, a longer lens and a more
distant wall look almost identical. COLMAP normally adjusts focal length during
reconstruction. In a test on a synthetic street with a true focal length of 853 px,
it drifted to 707 px horizontally and 1279 px vertically, and it placed the 15 m
facade at 12.4 m. The images still fitted closely, so the error was easy to miss.

## Decision

Always give COLMAP a known focal length, and never let it refine focal length,
principal point or distortion during reconstruction. Lens correction happens before
this step.

## Consequences

- With focal length fixed, the same test placed the facade at 15.00 m.
- StreetPano must get focal length from photo metadata or a calibration.
- Every photo in a sequence must use the same zoom setting.
- Distortion has to come from a lens database or calibration. Phone lenses may need
  a calibration step, which hasn't been tested yet.
