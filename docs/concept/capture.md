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
- **Walk at a steady pace.** Where the camera positions need joining at a gap, the
  scale on each side comes from the usual step size.
- **Choose a straight, flat 100 m first,** with few parked cars. Save hills and
  curves for later.

If a lens can't fit a whole building in the frame, don't walk further from it,
because the street width limits that. Shoot closer together, or walk the street
twice with the camera tilted to a different height.

## Stitching your photos

Copy the photos into their own folder, and then run:

```sh
./scripts/stitch.sh ~/Pictures/my-street
```

This reads the focal length from the photos, finds the camera positions, and opens the
app with the panorama. If the photos don't record a 35 mm equivalent focal length, add
the focal length in pixels after the folder name.

RAW photos need converting to JPEG first for now, which drops the focal length from
the converted files.

The results go in a `my-street.streetpano` folder next to your photos. Delete that
folder to start again, which also deletes your edits. Your original photos are never changed.

## Fixing objects in the panorama

Seams go around most objects by themselves, but they can also remove or repeat them. To
overrule them while the panorama is showing:

1. Click **Mark an Area** in the toolbar, and drag an outline around the object. Nothing
   changes yet.
2. A menu opens by itself. You can also right-click an outline at any time to open it.
3. Choose what should happen:

| Choice | What it does |
|---|---|
| **Remove This Object** | Fills the area from the photo that shows it cleanest |
| **Keep This Object Whole** | Takes all of the area from one photo |
| **Try a Different Photo** | If the result isn't right, fills the area from another photo |
| **Describe a Fix for the AI** | Saves your description with the area. The AI isn't built yet, so nothing changes for now |
| **Cancel or Delete This Area** | Removes the outline, and your edit if you've made one |

Finished edits show no outline, so the result looks clean. The **Edits** panel on the right
lists every edit:

- Click an edit to select it and zoom to it.
- Untick an edit to switch it off without deleting it, which is handy for comparing before
  and after.
- Drag edits up or down. The lowest one is drawn on top.
- Right-click an edit for the same choices as on the panorama.

**Show All Outlines** in the toolbar outlines every edit at once.

Undo and redo are in the toolbar too. You can zoom with the wheel, and pan by dragging with
the right mouse button. To save the panorama with your edits, choose **File > Save Panorama**.

Your edits are saved straight away in `decisions.json`, in the `.streetpano` folder, and come
back when you stitch again. They contain no pixels, and your photos are never changed. Zoom
with the wheel. While marking, pan with the right mouse button.

If you delete `poses.json` and find the camera positions again, the positions can change
slightly, so check your edits afterwards.

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
