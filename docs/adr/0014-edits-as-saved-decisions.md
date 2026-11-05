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

## Update: choices instead of keys

Asking users to step through photos with keys proved hard to understand, and square brackets
are awkward on a Swiss keyboard. So:

- An outline only marks an area. Nothing changes until a choice is made from a menu, which
  opens by itself and on right-click. The same choices are in a toolbar.
- **Remove** picks the photo closest to the median of the photos that see the area, because an
  object that's only in some of them is far from the median.
- **Keep whole** picks the photo looking most directly at the area.
- **Describing a fix for the AI** saves the words with the area and changes nothing, and the
  interface says so, because no AI exists yet.

On the same panorama, Remove took about 0.3 to 0.5 s in a debug build. It removed a parked
car's rear from the road, and two of three repeated vans. An area at the far end of the street
was refused, because fewer than 3 photos saw it.

## Results

On a real panorama of 16,154 x 3,961 px, marking an area or changing its photo took about
0.3 s in a debug build. In one test, 1 of 4 photos brought back towers that the seams had
removed, blending in well. Another was too bright, because it differs too much from the
area around it.

## Consequences

- Selecting an object with one click using AI, and correcting it from a description, can both
  build on this operation.
- Remove relies on most photos showing the area clean. An object seen by most of them, such as
  one that's parked in front of the facade, can stay.
- Edits run on the interface thread and redraw every area, so many areas will get slow.
- The automatic result is kept in memory for redrawing, which adds up to the size of the
  panorama, about 256 MB at the 64 MP limit, once the first edit is made.
- Edits are tied to the wall's shape. Finding camera positions again can move what's under
  an outline, because results vary between runs.
- Nothing yet warns about this, and old versions of `panorama.jpg` aren't kept.
