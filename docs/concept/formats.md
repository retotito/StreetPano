# File formats

| Stage | Format | Notes |
|---|---|---|
| **Input, first version** | RAW (CR3, NEF, ARW, DNG and others, via LibRaw), JPEG, TIFF | RAW recommended |
| **Input, later** | HEIC, video frames | Mainly for phones |
| **Internal processing** | 16-bit TIFF, plus small previews | Keeps quality through corrections |
| **Output** | BigTIFF master, JPEG or WebP viewer tiles | Standard TIFF stops at 4 GB, which a long street can exceed |

JPEG can't exceed 65,535 px per side, and WebP can't exceed 16,383 px. So long
panoramas use these formats only for tiles or split sections.

## RAW

Use the same development settings across the whole sequence. Separate per-photo
adjustments would create colour jumps at the seams.

## JPEG

JPEG is supported, but gives less room for correction. Automatic in-camera
adjustments should be turned off. Phone photos need particular care, because their
processing can vary from shot to shot.

## Lens correction

The lens is identified from the photo's metadata, for example using the Lensfun
database. Correction happens before anything else, because later steps assume
straight lines stay straight.
