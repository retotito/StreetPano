# 0004: Non-destructive project model

**Status:** Accepted

## Context

Users will adjust verticals, objects, seams and slope in any order. Editing a
rendered panorama would make earlier decisions impossible to change, and the output
is too large to re-render casually.

## Decision

A project stores references to the original photos plus a list of decisions. The
panorama is always rendered from those. Object masks are stored against **source
photos**, not panorama coordinates.

## Consequences

- Any decision can be changed without redoing the others.
- Masks stay correct when a vertical or slope setting changes the panorama.
- The project format needs versioning from the start.
- Rendering must support updating only the affected area.
