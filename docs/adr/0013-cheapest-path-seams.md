# 0013: Cheapest-path seams instead of a graph cut

**Status:** Accepted

## Context

Straight seams cut through cars and trees. The plan was a graph cut, but PyMaxflow and
the original Boykov-Kolmogorov code are GPL, which doesn't fit the Apache-2.0 licence.

## Decision

Draw photos from left to right. For each one, find the cheapest top-to-bottom path through
the overlap with what's already drawn, by dynamic programming:

- **Cost** is how much the two images disagree. Objects in front of the facade disagree
  between photos because of parallax, so the path runs around them.
- **Moving away from the strip's centre costs extra,** because views there are more
  oblique. The path only moves for a good reason.
- **Comparing with everything drawn so far,** not just the previous photo, lets a seam
  swing wide enough to replace a photo in some rows. That's how a car roof near the camera
  gets replaced by a neighbouring photo's road.

Each photo is first matched in brightness to what's drawn, using the medians of the
overlap, limited to between 0.8 and 1.25 times.

## Results

On the same 27 real photos, compared with straight seams:

| | Straight seams | Cheapest path |
|---|---|---|
| Stitch time | About 3 s | About 4.5 s |
| Peak memory | About 365 MB | About 570 MB |
| Cars and vans | Cut and squeezed | Mostly whole |
| Dark patches from nearby car roofs | 3 large ones | 1 large, 2 small |
| Brightness blocks | Visible | Much weaker |

These are judged by eye, apart from time and memory.

## Consequences

- There's no new dependency or licence question, and no graph to fit in memory.
- A seam is one path per photo, so it can't enclose an island. A graph cut could, so this
  decision can be revisited if that proves necessary.
- **Seams now edit the scene without asking.** On this street they removed a parked car
  and the towers seen through a gap, showed one van three times, clipped a letter and made
  two cars overlap. So users need a way to keep or remove objects, which can raise or lower
  the cost under them. See [Foreground objects](../concept/foreground.md).
- Matching each photo to what's drawn so far can slowly drift in brightness on long
  streets. This hasn't been tested.
