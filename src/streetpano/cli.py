"""Command-line interface for StreetPano."""

import argparse
from collections.abc import Sequence

from streetpano import __version__
from streetpano.geometry import (
    focal_length_px,
    ground_sample_distance_m,
    parallax_shift_px,
)
from streetpano.sizing import (
    BYTES_PER_GB,
    image_bytes,
    panorama_width_px,
    photo_count,
)


def build_parser() -> argparse.ArgumentParser:
    """Create the argument parser."""
    parser = argparse.ArgumentParser(
        prog="streetpano",
        description="Plan and build long street panoramas.",
    )
    parser.add_argument(
        "--version", action="version", version=f"%(prog)s {__version__}"
    )
    commands = parser.add_subparsers(dest="command", required=True)

    estimate = commands.add_parser(
        "estimate", help="estimate photo count, output size and parallax"
    )
    estimate.add_argument("--length", type=float, default=500, help="street, m")
    estimate.add_argument("--spacing", type=float, default=1, help="between photos, m")
    estimate.add_argument("--facade", type=float, default=15, help="to facade, m")
    estimate.add_argument("--height", type=int, default=4000, help="panorama, px")
    estimate.add_argument("--focal", type=float, default=24, help="lens, mm")
    estimate.add_argument("--sensor", type=float, default=36, help="sensor width, mm")
    estimate.add_argument("--image-width", type=int, default=6000, help="photo, px")
    estimate.add_argument(
        "--object", type=float, default=5, help="to foreground object, m"
    )
    return parser


def format_estimate(args: argparse.Namespace) -> str:
    """Return the estimate as readable text."""
    focal_px = focal_length_px(args.focal, args.sensor, args.image_width)
    gsd_m = ground_sample_distance_m(args.facade, focal_px)
    width_px = panorama_width_px(args.length, gsd_m)
    pixels = width_px * args.height
    shift_px = parallax_shift_px(focal_px, args.spacing, args.object, args.facade)
    size_8bit = image_bytes(pixels, bit_depth=8) / BYTES_PER_GB
    size_16bit = image_bytes(pixels, bit_depth=16) / BYTES_PER_GB
    return "\n".join(
        [
            f"Photos:          {photo_count(args.length, args.spacing)}",
            f"Facade detail:   {gsd_m * 1000:.2f} mm/px",
            f"Panorama:        {width_px} x {args.height} px",
            f"Uncompressed:    {size_8bit:.1f} GB (8-bit), "
            f"{size_16bit:.1f} GB (16-bit)",
            f"Foreground shift: {shift_px:.0f} px between neighbouring photos",
        ]
    )


def main(argv: Sequence[str] | None = None) -> int:
    """Run the command-line interface and return its exit code."""
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        print(format_estimate(args))
    except ValueError as error:
        parser.error(str(error))
    return 0
