# 0011: Check camera paths and join sections ourselves

**Status:** Accepted

## Context

The first real test was 27 phone photos of a street with two buildings, trees and
parked cars. With the settings from synthetic streets, COLMAP connected only 14.

Loosening COLMAP's settings connected more photos but gave wrong streets, while the
reprojection error stayed low, at about 1 px:

| Change | Photos | What went wrong |
|---|---|---|
| More image detail (4000 px) | 14 | No improvement |
| Modelling lens distortion | 14 + 12 | Still two parts |
| Accepting weaker matches | 26 | False 9 m jumps, and the street folded back |
| Guided matching and a looser ratio test | 27 | The last 17 cameras collapsed into one spot |
| Comparing 5 neighbours, with guided matching | 26 | One half at 60% scale, and 3 reruns all failed |

Locating cameras from one part using the other's 3D points put them about 5 photos
too far back, consistently, which fits windows matching the identical window a bay
along. Both parts were sound on their own. Joining them was what went wrong.

## Decision

1. Treat the reprojection error as insufficient, and check every result as a walk:
   no odd steps, no sudden turns and no sudden change of scale.
2. Split a result where its path breaks, and keep the sound parts.
3. Join neighbouring parts ourselves. The direction and rotation come from the two
   photos at the gap, and the scale and step length come from the usual step in each
   part.
4. Run the quick mapping step 5 times, and keep the result that covers the most photos
   with the most even steps. Matching stays a single, slower step.

## Results

Five full runs on those 27 photos all gave 26 photos in the right order, with steps
between 0.97 and 1.35 of a unit. Each took under a minute on an 8 GB laptop and used
about 2 GB.

## Consequences

- A steady walking pace is now assumed across a gap, so a change of pace there becomes
  a change of scale. Measuring the distance to the buildings was tried instead, but one
  tree or van can dominate a photo. It gave 8.68 and 5.05 for two ends of one part.
  Photo timestamps could improve this.
- Sections aren't refined together after joining. Doing so undid the correction, which
  suggests that false matches across the gap still pull the scale. Filtering them
  would allow it.
- A deliberate pause while shooting is seen as a break, then joined with a normal step.
- Results still vary between runs, including the angle of the facade plane.
- This has been measured on one real street. Long streets may behave differently, and
  5 attempts will take longer on them.
