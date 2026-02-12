#!/usr/bin/env python3
"""
Quick smoke-test runner for the Instant-NGP training step.

Usage (defaults assume repo root is C:\CrossHatch-JRDW):
  python run_nerf_test.py
  python run_nerf_test.py --images "out/build/x64-debug/mono/images/no_bg" --steps 20

This imports `train_nerf` from `nerf_pipeline.py` and runs a small number of steps
so you can validate the training path without running COLMAP or preprocessing.
"""
import sys
from pathlib import Path
import argparse
import json

repo_root = Path(__file__).parent
sys.path.insert(0, str(repo_root))

try:
    from nerf_pipeline import train_nerf
except Exception as e:
    print(f"Error importing nerf_pipeline.train_nerf: {e}")
    raise

def main():
    parser = argparse.ArgumentParser(description="Run a quick NeRF training smoke test")
    default_images = repo_root / "out" / "build" / "x64-debug" / "mono" / "images" / "no_bg"
    default_output = repo_root / "out" / "build" / "x64-debug" / "mono" / "model_test.ingp"
    default_progress = repo_root / "out" / "build" / "x64-debug" / "mono" / "progress_test.json"

    parser.add_argument('--images', '-i', default=str(default_images), help='Path to images directory (or parent)')
    parser.add_argument('--output', '-o', default=str(default_output), help='Output .ingp model path')
    parser.add_argument('--steps', '-s', type=int, default=10, help='Number of training steps to run')
    parser.add_argument('--progress', '-p', default=str(default_progress), help='Progress JSON file')

    args = parser.parse_args()

    images_dir = args.images
    output_path = args.output
    n_steps = args.steps
    progress_file = args.progress

    print(f"[test] Running NeRF smoke test")
    print(f"[test] Images dir: {images_dir}")
    print(f"[test] Output path: {output_path}")
    print(f"[test] Steps: {n_steps}")
    print(f"[test] Progress file: {progress_file}")

    result = train_nerf(images_dir, output_path, n_steps=n_steps, progress_file=progress_file)

    print("[test] Result:")
    print(json.dumps(result, indent=2))

    if result.get('success'):
        print("[test] Smoke test succeeded")
        sys.exit(0)
    else:
        print("[test] Smoke test failed")
        sys.exit(1)

if __name__ == '__main__':
    main()
