#!/bin/sh
# Finds camera positions for a folder of photos, then opens the app and stitches them.
# Usage: ./scripts/stitch.sh <folder of photos> [focal length in pixels]
set -eu

if [ $# -lt 1 ]; then
    echo "Usage: $0 <folder of photos> [focal length in pixels]" >&2
    exit 2
fi

root="$(cd "$(dirname "$0")/.." && pwd)"
photos="$(cd "$1" && pwd)"
work="$photos.streetpano"
cd "$root"

cmake --preset release >/dev/null
cmake --build --preset release --target streetpano_app

if [ ! -f "$work/poses.json" ]; then
    # Only COLMAP's own working files are replaced, never a panorama or saved edits.
    rm -rf "$work/database.db" "$work/sparse"
    # Keep the Mac awake, because sleeping pauses this step.
    echo "Finding camera positions. This can take several minutes..."
    if [ $# -ge 2 ]; then
        caffeinate -i uv run --project tools streetpano poses "$photos" "$work" --focal-px "$2"
    else
        caffeinate -i uv run --project tools streetpano poses "$photos" "$work"
    fi
fi

case "$(uname -s)" in
    Darwin) exec "$root/build/release/app/StreetPano.app/Contents/MacOS/StreetPano" --stitch "$photos" ;;
    *) exec "$root/build/release/app/StreetPano" --stitch "$photos" ;;
esac
