# AI models

No custom training is planned. Existing models cover each job, and the real work is
the pipeline, seam logic and review tool.

All models sit behind replaceable interfaces. None of them is required for the
classic baseline, and model weights are never stored in this repository.

| Job | Candidate | Status |
|---|---|---|
| Suggest objects | Mask2Former or similar, pretrained on street scenes | Ready to use |
| Outline an object from a click | SAM 2 | Ready to use |
| Follow an object across photos | SAM 2 with camera positions | **Needs testing** at 1 m spacing |
| Estimate depth, only if needed | Depth Anything 3 | Ready to use |
| Match photos and find positions | COLMAP, optionally LightGlue | Ready to use |
| Fill small gaps | LaMa | Quality needs testing |
| Fill larger gaps | Diffusion inpainting, DiffuEraser | Quality needs testing |

## When training might make sense

Only if testing shows a specific, repeated failure. The first option is
**fine-tuning** an existing model on a few hundred examples, which user corrections
could supply. Training a new model from scratch is not planned.

## Licences

Checked on 2026-11-04 against each project's GitHub repository. Licences change, so
check again before adding a dependency.

**A repository's licence does not always cover its weights.** Check both.

| Project | Licence | Notes |
|---|---|---|
| [SAM 2](https://github.com/facebookresearch/sam2) | Apache-2.0 | |
| [Depth Anything 3](https://github.com/ByteDance-Seed/Depth-Anything-3) | Varies by model | Small, Base and Metric-Large are Apache-2.0. Large, Giant and Nested are CC BY-NC 4.0. |
| [Mask2Former](https://github.com/facebookresearch/Mask2Former) | MIT | Repository archived. Check the licence of the training data for pretrained weights. |
| [COLMAP](https://github.com/colmap/colmap) | New BSD | Now includes the GLOMAP global mapper. Some dependencies are licensed separately. |
| [LightGlue](https://github.com/cvg/LightGlue) | Apache-2.0 | Feature extractors may have different terms. |
| [VGGT](https://github.com/facebookresearch/vggt) | Custom | Only a specific checkpoint allows commercial use. |
| [LaMa](https://github.com/advimman/lama) | Apache-2.0 | |
| [DiffuEraser](https://github.com/lixiaowen-xw/DiffuEraser) | Apache-2.0 | Check the base models it depends on. |
| [ProPainter](https://github.com/sczhou/ProPainter) | S-Lab 1.0 | **Non-commercial only.** |
| [Netflix VOID](https://github.com/Netflix/void-model) | Apache-2.0 | [Reportedly needs 40 GB+ of VRAM](https://aitoolsatlas.ai/blog/netflix-void-ai-video-object-removal-open-source-2026). |
| [UDIS++](https://github.com/nie-lang/UDIS2) | Apache-2.0 | |

## Libraries

| Project | Licence | Notes |
|---|---|---|
| [OpenSeadragon](https://github.com/openseadragon/openseadragon) | BSD-3-Clause | |
| [libvips](https://github.com/libvips/libvips) | LGPL-2.1 | |
| [LibRaw](https://github.com/LibRaw/LibRaw) | LGPL-2.1 or CDDL-1.0 | |
| [rawpy](https://github.com/letmaik/rawpy) | MIT | |
| [Lensfun](https://github.com/lensfun/lensfun) | LGPL-3.0 | |
| [NumPy](https://github.com/numpy/numpy) | BSD-3-Clause | In use |
| [Pillow](https://github.com/python-pillow/Pillow) | MIT-CMU | In use |
| [PyMaxflow](https://github.com/pmneila/PyMaxflow) | **GPL** | **Avoid as a required dependency.** See below. |

## Open question: graph-cut library

PyMaxflow is GPL, which conflicts with the plan to distribute StreetPano under
Apache-2.0 if it's a required dependency. Options to evaluate:

1. OpenCV's graph-cut seam finder, which only handles pairs of images.
2. Our own max-flow implementation in Rust, which isn't a large algorithm.
3. PyMaxflow as an optional research-only dependency, to compare results.

See the [licensing decision record](../adr/0006-code-and-model-licensing.md).
