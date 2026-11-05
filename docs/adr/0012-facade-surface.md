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
- The wall's distances are plain data, one per camera. A later manual correction can
  change them without redoing other work.
- Rooflines still wave slightly near trees, where branches affect the measurement.
- Foreground objects are still cut at seams, and the road still isn't on the wall.
- This has been measured on one real street, and on synthetic flat streets.
