# 0014: Edits are saved decisions, drawn from the original photos

**Status:** Accepted

## Context

Automatic seams sometimes remove or repeat real objects, so users need to overrule them.
Users also expect any edit to be reversible, and for their original photos to stay safe.

## Decision

The basic edit is one operation: **this outlined area of the wall takes its pixels from this
photo.** Keeping an object whole and removing one are the same operation, with a different
photo chosen.

- Edits are saved in `decisions.json`, beside the camera positions, as soon as they're made.
  The file holds outlines and photo names, and no pixels.
- Outlines are stored as positions on the wall, not panorama pixels, so they survive a change
  of panorama size.
- The panorama is the automatic result plus the decisions, drawn from the original photos.
  So undo, redo, deleting an area and choosing another photo all work the same way.
- A stitch loads the saved decisions, so edits come back after stitching again.
- `decisions.json` and `panorama.jpg` are written to a temporary file and then swapped into
  place, so an interrupted save can't damage them.
- Finding camera positions no longer deletes the work folder, only COLMAP's own files.

## Results

On a real panorama of 16,154 x 3,961 px, marking an area or changing its photo took about
0.3 s in a debug build. In one test, 1 of 4 photos brought back towers that the seams had
removed, blending in well. Another was too bright, because it differs too much from the
area around it.

## Consequences

- Choosing between photos is manual for now. Choosing automatically, and selecting an object
  with one click using AI, can both build on this operation.
- Edits run on the interface thread and redraw every area, so many areas will get slow.
- The automatic result is kept in memory for redrawing, which adds up to the size of the
  panorama, about 256 MB at the 64 MP limit, once the first edit is made.
- Edits are tied to the wall's shape. Finding camera positions again can move what's under
  an outline, because results vary between runs.
- Nothing yet warns about this, and old versions of `panorama.jpg` aren't kept.
