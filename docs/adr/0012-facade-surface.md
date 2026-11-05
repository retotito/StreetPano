# 0012: A facade surface instead of one plane

**Status:** Accepted

## Context

The first stitcher projected the whole street onto one plane, found with RANSAC. On a
real street with two buildings at an angle to each other:

- The plane's angle varied between runs, depending on which building it settled on.
- Panoramas came out tilted and wedge-shaped.
- Letters on the building further from the plane were lost at the seams.

## Decision

Build a vertical wall that follows the street:

1. Take "up" from the average camera direction.
2. At each camera, measure the distance to the most crowded depth, at right angles to
   the walk. Use only 3D points above camera height, which leaves out the road and
   most parked cars.
3. Ignore depths outside 0.6 to 1.6 times the street's typical distance. Fill the gaps
   from the neighbouring cameras, so that a gap between buildings doesn't push the wall
   back to what's seen through it.
4. Smooth the distances with a median filter, and limit how sharply they can change.
5. Lay the wall out flat: distance along it runs across the panorama, and height runs
   up it.

## Update: straight facades and aligned seams

Measuring one distance per camera let tree branches pull the wall closer, which made
buildings zigzag. A rise in the wall's distance also shows up as a change of scale, so
the error grew with height: lines near camera height stayed flat, while rooflines waved.

- Seen from above a facade is a straight line, and branches aren't. So each camera now
  fits a line through the points within 4 steps either way, with RANSAC.
- Neighbouring photos agree only when the wall is at the right distance. So at each
  seam, nearby distances are tried on the photos themselves, and the best one moves the
  wall. Weak matches and answers at the edge of the search are ignored, because branches
  caused jumps of up to 15% there.

Lines alone straightened the bands but doubled a letter in 2 of 5 runs. With seam
alignment, the letter was clean in all 5, and the seams on the facade had no sharp kinks.
This is judged by eye. The mean match at seams stayed at about 0.78, so it doesn't confirm
the improvement, partly because the branches at some seams never match well. A wall made
10% too near still showed doubled letters, so this corrects small errors only.

## Results

On 5 sets of camera positions for the same 27 real photos:

| | One plane | Facade surface |
|---|---|---|
| Panorama width | 10,737 to 13,677 px | 16,022 to 16,323 px |
| Panorama height | 4,679 to 5,960 px | 3,920 to 3,994 px |
| Shape | Tilted, in some runs a wedge | Straight, in every run |
| "EDELWERK" and "TERTIANUM" | Often missing letters | Complete |

## Consequences

- A straight panorama is now the default, because the wall is always laid out flat.
- The wall's distances are plain data, one per camera (`FacadeSurface::samples`). A later
  manual correction can change them through `withDepthOffsets` without redoing other work.
- A gentle bow remains in some rooflines, and users can't correct it by hand yet.
- Foreground objects are still cut at seams, and the road still isn't on the wall.
- This has been measured on one real street, and on synthetic flat streets.
