#!/usr/bin/env python3
"""
Test script to demonstrate the integrated background removal functionality
with colmap2nerf.py
"""

import os
import sys
import subprocess

def test_colmap_with_background_removal():
    """
    Test the colmap2nerf.py script with background removal enabled.
    This is a demonstration of how to use the new functionality.
    """
    
    # Example command line arguments for colmap2nerf.py with background removal
    example_command = [
        "python", "colmap2nerf.py",
        "--images", "path/to/your/images",
        "--run_colmap",
        "--remove_background",  # This is the new flag
        "--colmap_matcher", "sequential",
        "--aabb_scale", "32"
    ]
    
    print("Example usage of colmap2nerf.py with background removal:")
    print(" ".join(example_command))
    print("\nThis will:")
    print("1. Extract features from images")
    print("2. Remove backgrounds from all images using rembg")
    print("3. Run feature matching on the processed images")
    print("4. Continue with the rest of the COLMAP pipeline")
    print("\nNote: Make sure you have 'rembg' installed:")
    print("pip install rembg")

if __name__ == "__main__":
    test_colmap_with_background_removal()
