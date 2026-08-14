#!/usr/bin/env python3
"""Export an open_clip image encoder to ONNX for the IDHANEmbedding module.

Run once per model, offline. Produces a directory the module can load:

    <out>/<model_name>/model.onnx
    <out>/<model_name>/model.json

model.json is written from open_clip's OWN preprocess config, never by hand. That is the point of
this script: the preprocessing has to match how the model was trained, and a mean/std/resize mode
copied by a human is a value that can silently drift from the checkpoint it belongs to.

Usage:
    export_siglip2.py --model-dir /path/to/ViT-B-16-SigLIP2 --out /path/to/build/bin/models
"""

import argparse
import json
import pathlib
import sys

import open_clip
import torch


class ImageEncoder(torch.nn.Module):
    """encode_image plus the L2 normalisation, so the graph emits unit vectors directly.

    Folding the normalisation in means no consumer can forget it. The module still checks the norm
    of what comes back, but that check exists to catch a bad export -- not to do the work.
    """

    def __init__(self, model):
        super().__init__()
        self.model = model

    def forward(self, pixel_values):
        features = self.model.encode_image(pixel_values)
        return torch.nn.functional.normalize(features, dim=-1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--model-dir", required=True, type=pathlib.Path,
                        help="Directory holding open_clip_config.json and the checkpoint")
    parser.add_argument("--out", required=True, type=pathlib.Path,
                        help="Models root; a subdirectory named after the model is created inside it")
    parser.add_argument("--opset", type=int, default=17)
    args = parser.parse_args()

    model_dir = args.model_dir.resolve()
    config_path = model_dir / "open_clip_config.json"

    if not config_path.is_file():
        print(f"error: {config_path} does not exist", file=sys.stderr)
        return 1

    with config_path.open() as handle:
        raw_config = json.load(handle)

    model_cfg = raw_config["model_cfg"]
    preprocess_cfg = raw_config["preprocess_cfg"]

    model_name = model_dir.name
    dimensions = int(model_cfg["embed_dim"])
    image_size = model_cfg["vision_cfg"]["image_size"]
    if isinstance(image_size, int):
        image_size = [image_size, image_size]

    resize_mode = preprocess_cfg.get("resize_mode", "squash")
    if resize_mode != "squash":
        print(f"error: resize_mode '{resize_mode}' is not supported; only 'squash' is", file=sys.stderr)
        return 1

    print(f"loading {model_name} ({dimensions} dims, {image_size[0]}x{image_size[1]}, {resize_mode})")

    # Same call HyCLIP_Model.__init__ makes, so there is no second opinion about how the model loads.
    model, _ = open_clip.create_model_from_pretrained(f"local-dir:{model_dir}")
    model.eval()

    wrapper = ImageEncoder(model)
    wrapper.eval()

    out_dir = args.out.resolve() / model_name
    out_dir.mkdir(parents=True, exist_ok=True)
    onnx_path = out_dir / "model.onnx"

    example = torch.randn(2, 3, image_size[1], image_size[0])

    print(f"exporting to {onnx_path}")
    with torch.no_grad():
        torch.onnx.export(
            wrapper,
            example,
            str(onnx_path),
            input_names=["pixel_values"],
            output_names=["image_features"],
            # The dynamic batch axis is load-bearing, not an optimisation.
            dynamic_axes={"pixel_values": {0: "batch"}, "image_features": {0: "batch"}},
            opset_version=args.opset,
            do_constant_folding=True,
            dynamo=False,
        )

    model_json = {
        "model_name": model_name,
        "dimensions": dimensions,
        "image_size": [int(image_size[0]), int(image_size[1])],
        "resize_mode": resize_mode,
        "mean": [float(v) for v in preprocess_cfg["mean"]],
        "std": [float(v) for v in preprocess_cfg["std"]],
        "input_name": "pixel_values",
        "output_name": "image_features",
        "normalized_output": True,
    }

    config_out = out_dir / "model.json"
    with config_out.open("w") as handle:
        json.dump(model_json, handle, indent=2)
        handle.write("\n")

    print(f"wrote {config_out}")
    print(json.dumps(model_json, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
