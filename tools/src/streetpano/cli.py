"""Command-line interface for StreetPano."""

import argparse
import json
from collections.abc import Sequence
from pathlib import Path

from streetpano import __version__
from streetpano.geometry import (
    focal_length_px,
    ground_sample_distance_m,
    parallax_shift_px,
)
from streetpano.poses import (
    PoseEstimationError,
    compare_with_truth,
    estimate_poses,
    write_estimate,
)
from streetpano.sizing import (
    BYTES_PER_GB,
    image_bytes,
    panorama_width_px,
    photo_count,
)
from streetpano.synthetic import CameraSpec, default_scene, write_dataset


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
    estimate.set_defaults(run=run_estimate)

    synth = commands.add_parser(
        "synth", help="render a synthetic test street with exact ground truth"
    )
    synth.add_argument("output", type=Path, help="new or empty folder")
    synth.add_argument("--length", type=float, default=30, help="street, m")
    synth.add_argument("--spacing", type=float, default=1, help="between photos, m")
    synth.add_argument("--seed", type=int, default=0, help="for a repeatable street")
    synth.add_argument("--width", type=int, default=1280, help="photo, px")
    synth.add_argument("--height", type=int, default=720, help="photo, px")
    synth.add_argument("--camera-height", type=float, default=1.6, help="m")
    synth.add_argument("--slope", type=float, default=0, help="0.05 means 5%%")
    synth.add_argument("--pitch", type=float, default=0, help="camera tilt, degrees")
    synth.add_argument("--roll", type=float, default=0, help="camera roll, degrees")
    synth.add_argument("--wobble", type=float, default=0, help="hand-held path, m")
    synth.add_argument(
        "--exposure-jitter", type=float, default=0, help="brightness change, fraction"
    )
    synth.set_defaults(run=run_synth)

    poses = commands.add_parser(
        "poses", help="find camera positions from photos with COLMAP"
    )
    poses.add_argument("photos", type=Path, help="folder of photos")
    poses.add_argument("output", type=Path, help="new or empty folder")
    poses.add_argument(
        "--focal-px", type=float, required=True, help="known focal length, px"
    )
    poses.add_argument(
        "--truth", type=Path, help="scene.json of a synthetic street, to measure error"
    )
    poses.set_defaults(run=run_poses)
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


def run_estimate(args: argparse.Namespace) -> None:
    """Print the capture estimate."""
    print(format_estimate(args))


def run_synth(args: argparse.Namespace) -> None:
    """Render a synthetic street to a folder."""
    scene = default_scene(
        length_m=args.length,
        spacing_m=args.spacing,
        seed=args.seed,
        camera=CameraSpec(width_px=args.width, height_px=args.height),
        camera_height_m=args.camera_height,
        slope=args.slope,
        pitch_deg=args.pitch,
        roll_deg=args.roll,
        path_wobble_m=args.wobble,
        exposure_jitter=args.exposure_jitter,
    )
    write_dataset(scene, args.output)
    print(f"Wrote {scene.photo_count} photos to {args.output}")


def run_poses(args: argparse.Namespace) -> None:
    """Find camera positions, and measure them if the truth is known."""
    estimate = estimate_poses(args.photos, args.output, args.focal_px)
    write_estimate(estimate, args.output / "poses.json")
    print(f"Registered {len(estimate.poses)} photos")
    if estimate.missing:
        print(f"Missing: {', '.join(estimate.missing)}")
    print(f"Mean reprojection error: {estimate.mean_reprojection_error_px:.2f} px")
    if args.truth is not None:
        description = json.loads(args.truth.read_text(encoding="utf-8"))
        report = compare_with_truth(estimate, description)
        print(f"Rotation error:  {report.rotation_median_deg:.3f} deg median")
        print(f"Position error:  {report.position_median_m * 100:.1f} cm median")
        print(f"Facade distance: {report.facade_distance_m:.2f} m")


def main(argv: Sequence[str] | None = None) -> int:
    """Run the command-line interface and return its exit code."""
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        args.run(args)
    except (ValueError, FileExistsError, PoseEstimationError) as error:
        parser.error(str(error))
    return 0
