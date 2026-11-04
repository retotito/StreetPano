#!/bin/sh
# Builds the app and opens it, using a synthetic street unless given a folder of photos.
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

cmake --preset dev >/dev/null
cmake --build --preset dev --target streetpano_app

folder="${1:-}"
if [ -z "$folder" ]; then
    folder="$root/build/demo-street"
    if [ ! -d "$folder" ]; then
        uv run --project tools streetpano synth "$folder" --length 30 --seed 7 \
            --wobble 0.02 --exposure-jitter 0.02
    fi
    folder="$folder/photos"
fi

case "$(uname -s)" in
    Darwin) exec "$root/build/dev/app/StreetPano.app/Contents/MacOS/StreetPano" "$folder" ;;
    *) exec "$root/build/dev/app/StreetPano" "$folder" ;;
esac
