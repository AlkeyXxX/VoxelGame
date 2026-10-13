#!/usr/bin/env python3
"""Convert the source 7DTD biome image and splat3 road overlay to runtime RAW masks.

Requires: Python 3, Pillow, NumPy.
The heightmap dtm.raw is not converted: copy it byte-for-byte from the source world.

Example:
  python prepare_runtime_assets.py --biomes biomes.png --splat3 splat3.png --output-dir ./runtime

Output:
  biomes.raw       1024x1024 uint8 class IDs: 1 forest, 2 desert, 3 snow,
                   4 burnt forest, 5 wasteland.
  roadmask_2bit.raw 8192x8192 pixels, 2 bits each, four pixels per byte,
                    row-major, least-significant field first:
                    0 none, 1 red/asphalt, 2 green/dirt, 3 unused.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

import numpy as np
from PIL import Image

BIOME_PALETTE = (
    ((0, 64, 0, 255), 1),
    ((255, 168, 0, 255), 2),
    ((255, 255, 255, 255), 3),
    ((255, 228, 119, 255), 4),
    ((186, 0, 255, 255), 5),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def convert_biomes(source: Path, destination: Path) -> None:
    with Image.open(source) as original:
        image = original.convert("RGBA")
        if image.size != (1024, 1024):
            raise ValueError(f"Expected biomes.png to be 1024x1024; got {image.size}")
        counts = np.zeros(6, dtype=np.int64)
        with destination.open("wb") as output:
            for y in range(1024):
                row = np.asarray(image.crop((0, y, 1024, y + 1)), dtype=np.uint8).reshape(1024, 4)
                ids = np.zeros(1024, dtype=np.uint8)
                for rgba, biome_id in BIOME_PALETTE:
                    mask = np.all(row == np.asarray(rgba, dtype=np.uint8), axis=1)
                    ids[mask] = biome_id
                    counts[biome_id] += int(mask.sum())
                output.write(ids.tobytes())
    if int(counts.sum()) != 1024 * 1024:
        raise RuntimeError("Biome pixel accounting failed")
    if int(counts[1:].sum()) != 1024 * 1024:
        raise ValueError("biomes.png contains colors outside the documented five-color palette")
    print(f"biomes.raw: {destination.stat().st_size:,} bytes; IDs 1-5 counts={counts[1:].tolist()}")


def convert_roads(source: Path, destination: Path) -> None:
    width = height = 8192
    counts = np.zeros(4, dtype=np.int64)
    red_rgba = np.asarray((255, 0, 0, 255), dtype=np.uint8)
    green_rgba = np.asarray((0, 255, 0, 255), dtype=np.uint8)
    with Image.open(source) as original:
        image = original.convert("RGBA")
        if image.size != (width, height):
            raise ValueError(f"Expected splat3.png to be 8192x8192; got {image.size}")
        with destination.open("wb") as output:
            for y in range(height):
                row = np.asarray(image.crop((0, y, width, y + 1)), dtype=np.uint8).reshape(width, 4)
                codes = np.zeros(width, dtype=np.uint8)
                red = np.all(row == red_rgba, axis=1)
                green = np.all(row == green_rgba, axis=1)
                codes[red] = 1
                codes[green] = 2
                counts[0] += int((codes == 0).sum())
                counts[1] += int((codes == 1).sum())
                counts[2] += int((codes == 2).sum())
                counts[3] += int((codes == 3).sum())
                packed = (codes[0::4] |
                          (codes[1::4] << 2) |
                          (codes[2::4] << 4) |
                          (codes[3::4] << 6))
                output.write(packed.tobytes())
    expected = (width * height + 3) // 4
    if destination.stat().st_size != expected:
        raise RuntimeError(f"Packed road mask size mismatch: {destination.stat().st_size} != {expected}")
    print(f"roadmask_2bit.raw: {destination.stat().st_size:,} bytes; pixel codes 0-3 counts={counts.tolist()}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--biomes", required=True, type=Path, help="path to original biomes.png")
    parser.add_argument("--splat3", required=True, type=Path, help="path to original splat3.png")
    parser.add_argument("--output-dir", required=True, type=Path, help="directory for generated RAW masks")
    args = parser.parse_args()
    if not args.biomes.is_file() or not args.splat3.is_file():
        raise FileNotFoundError("Input biomes.png or splat3.png was not found")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    biome_out = args.output_dir / "biomes.raw"
    road_out = args.output_dir / "roadmask_2bit.raw"
    convert_biomes(args.biomes, biome_out)
    convert_roads(args.splat3, road_out)
    print(f"SHA-256 biomes.raw: {sha256(biome_out)}")
    print(f"SHA-256 roadmask_2bit.raw: {sha256(road_out)}")


if __name__ == "__main__":
    main()
