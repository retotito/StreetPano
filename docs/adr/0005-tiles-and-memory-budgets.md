# 0005: Tiles and memory budgets

**Status:** Accepted

## Context

Panoramas reach many gigabytes, and the project should run on computers with 8 GB
of RAM. Keeping a fixed number of whole photos in memory fails when zoomed out,
because the view can span hundreds of photos.

## Decision

Use a tile pyramid for viewing, streaming and sectioned processing for the pipeline,
and a job queue that enforces a user-set memory budget. Memory use must not depend
on street length.

## Consequences

- Very long streets are possible on modest computers.
- Algorithms must work section by section, which makes seam finding more complex.
- Disk space becomes the next limit, so caches need size limits as well.
- An automated test has to enforce the memory budget.
