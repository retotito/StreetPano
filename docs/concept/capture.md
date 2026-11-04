# Capture guide

!!! note "Untested recommendations"
    This guidance comes from geometry and general photography practice. It should
    be updated after the first test street.

## Photos or video?

Use **photos every 1–1.5 m**, which is about one shot per second at walking speed.
Closer spacing gives each part of the facade more viewpoints, so the software has
more real pixels to choose from behind each obstacle.

| | Photos | Video |
|---|---|---|
| Detail | About 4 mm/px with a 24 MP camera | About 6 mm/px at 4K |
| Quality | RAW, little compression | Compressed |
| Viewpoints | Fewer | Very dense |
| Object tracking | Harder across large gaps | Natural fit |
| Amount of data | Smaller | Larger |

This choice [needs a side-by-side test](../adr/0003-photos-as-main-capture.md).

## Camera settings

- Lock exposure, white balance and focus.
- Use a shutter speed of 1/500 s or faster.
- Turn off auto HDR, in-camera lens correction and similar automatic adjustments.
- Shoot RAW if possible. See [File formats](formats.md).

## Keep the zoom fixed

StreetPano needs the true focal length, so
[every photo in a sequence must use the same zoom](../adr/0008-fixed-focal-length.md).
Don't zoom during a walk, and on a phone, stay on one camera lens.

## How to walk

- Stay at a steady distance from the facade.
- Keep the camera level and pointed straight at the buildings.
- Leave extra space above the rooftops, because correcting tilt crops the image.
- Overlap the start and end of each section when you pause.

## When to shoot

- Choose overcast light if possible, because moving shadows and bright sun make
  photos harder to match.
- Choose quiet times, so there are fewer people and moving cars.
- Consider two passes at different times, because parked cars and people may have
  moved between them.

## First test with a smartphone

A phone is a good way to try the workflow before committing to better equipment.

- **Use the main camera.** The ultrawide usually has far less detail and more
  distortion. Avoid digital zoom.
- **Use the default resolution.** Phones with a 50 MP sensor usually combine pixels
  into a roughly 12 MP photo, which tends to have less noise.
- **Shoot photos as well as video.** Many budget phones stop at 1080p video, which
  has about half the detail of a 12 MP photo. The video is still useful for comparison.
- **Lock everything you can.** The open-source
  [Open Camera](https://opencamera.org.uk/) app has an auto-repeat mode with a set
  delay, exposure lock and, through its Camera2 setting, manual white balance. What
  works depends on the phone, so try it at home first.
- **Turn off automatic processing,** such as AI scene modes, HDR and beauty filters.
  This processing can vary between shots, which creates colour jumps at the seams.
- **Hold the phone sideways,** so that each photo covers more of the street, unless
  you need upright framing to fit tall buildings. Keep the same orientation throughout.
- **Avoid digital zoom and lens switching.** Some phones switch lenses by themselves,
  so check that the photos all look the same width.
- **Choose a straight, flat 100 m first,** with few parked cars. Save hills and
  curves for later.

If a lens can't fit a whole building in the frame, don't walk further from it,
because the street width limits that. Shoot closer together, or walk the street
twice with the camera tilted to a different height.

## Parked cars: use a higher camera

A row of parked cars hides the lower facade from every position along the street,
so walking further cannot reveal it. For a 1.5 m tall car about 5–7 m from the
camera and a facade 15 m away:

![From eye height, the bottom 1.4 m is hidden. From a 3 m pole, the whole facade is visible](../assets/occlusion.svg)

If you need the ground floors behind parked cars, a pole gives you real pixels
where the alternative is AI-generated ones. A pole also makes it harder to keep
the camera steady and level, so test it first.

## Privacy

If you publish panoramas, faces and number plates may need to be blurred. Check
the rules that apply where you shoot and publish.
