# 0002: Geometry first, AI assists

**Status:** Accepted

## Context

Parallax follows from known geometry. AI could be used to stitch directly, but deep
stitching methods work on image pairs, and generative models can invent believable
but false details.

## Decision

Use camera geometry for alignment and graph cuts for seams. AI identifies objects
and fills only what no photo shows. Generated pixels are always recorded in a mask.

## Consequences

- A useful baseline exists without any AI, and it's testable against known geometry.
- Output stays trustworthy for documentation.
- Results may be limited where a street has few usable viewpoints.
- No custom model training is planned.
