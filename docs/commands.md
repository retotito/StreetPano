# Commands

Run these in the Terminal app. Each block starts by going to the project folder.

## Open the app

```sh
cd ~/projects/StreetPano
./scripts/dev.sh ~/Downloads/Photos-1-001-jpeg
```

The app opens on **Projects**, where you can create a project for a folder of photos or open a
recent one. The steps along the top take a street from photos to a finished panorama:
**Projects**, **① Photos**, **② Align**, **③ Edit** and **④ Export**. In **② Align**, the
photos are shown side by side. **Add Horizontal Guide** puts a line across all of them, which
you can drag to a roofline or window line. Right-click a guide, or drag it off the photos, to
remove it. **Stitch Panorama** is at the end of this step.

## Stitch your photos

```sh
cd ~/projects/StreetPano
./scripts/stitch.sh ~/Downloads/Photos-1-001-jpeg
```

Swap in the folder with your photos. The first time, this finds the camera positions,
which takes a minute or two. After that it only stitches, which takes a few seconds. Then
the app opens with the panorama.

Results go in a folder with the same name plus `.streetpano`, next to your photos:

| File | What it is |
|---|---|
| `panorama.jpg` | The panorama, with your edits |
| `decisions.json` | Your edits |
| `poses.json` | The camera positions |

## Photos without a recorded focal length

If the script says the focal length is missing, add it in pixels after the folder:

```sh
cd ~/projects/StreetPano
./scripts/stitch.sh ~/Downloads/Photos-1-001-jpeg 3290
```

3290 is right for the Redmi 12's main camera at full size.

## RAW photos

The app doesn't read RAW yet. This converts DNG files to JPEG copies in a new folder,
and leaves the originals as they are:

```sh
mkdir -p ~/Downloads/Photos-1-001-jpeg
for f in ~/Downloads/Photos-1-001/*.dng; do
  sips -s format jpeg -s formatOptions 95 "$f" --out ~/Downloads/Photos-1-001-jpeg/"$(basename "$f" .dng).jpg"
done
```

The copies lose the focal length, so stitch them with the focal length as above.

## Start again

To find the camera positions again, delete `poses.json` and run the stitch command. Your
edits are kept, but check them afterwards, because the positions can change slightly.

```sh
rm ~/Downloads/Photos-1-001-jpeg.streetpano/poses.json
```

To delete everything, including your edits, delete the whole `.streetpano` folder. Your
photos are never changed.

## Try the app without photos

This opens the app with a made-up street, which takes about a minute the first time:

```sh
cd ~/projects/StreetPano
./scripts/dev.sh
```

## Run the tests

For development. This builds everything and runs the C++ and Python tests:

```sh
cd ~/projects/StreetPano
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cd tools && uv run pytest
```
