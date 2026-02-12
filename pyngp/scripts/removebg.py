from rembg import remove
from PIL import Image
import io
import os
import glob
from pathlib import Path
import gc
import sys
import subprocess

# Clear Numba cache to avoid corruption issues
# This fixes "OSError: [WinError -529697949] Windows Error 0xe06d7363"
numba_cache = os.path.expanduser("~/.numba_cache")
if os.path.exists(numba_cache):
    try:
        import shutil
        shutil.rmtree(numba_cache, ignore_errors=True)
        print(f"[rembg] Cleared Numba cache at {numba_cache}")
    except:
        pass

# Disable Numba parallelization to avoid memory issues
os.environ["NUMBA_NUM_THREADS"] = "1"
os.environ["NUMBA_CACHE_DIR"] = os.path.join(os.path.dirname(__file__), ".numba_cache")

def remove_background(input_path, output_path):
    """
    Removes the background from an image using rembg.

    Args:
        input_path (str): Path to the input image file.
        output_path (str): Path to save the output image with background removed.
    """
    try:
        input_path = Path(input_path)
        if not input_path.exists():
            print(f"Error processing {input_path}: File not found")
            return False
        
        # Open the input image
        with Image.open(input_path) as img:
            output_data = remove(img)
            output_data.save(output_path)
            print(f"[rembg] Processed: {input_path.name}")
            
            # Force garbage collection after each image to reduce memory usage
            gc.collect()
            return True

    except FileNotFoundError:
        print(f"Error: Input file not found at {input_path}")
        return False
    except Exception as e:
        print(f"[rembg] Error processing {input_path}: {e}")
        import traceback
        traceback.print_exc()
        return False

def remove_backgrounds_batch(images_folder, output_folder=None):
    """
    Removes backgrounds from all images in a folder.

    Args:
        images_folder (str): Path to the folder containing input images.
        output_folder (str): Path to save the output images. If None, creates no_bg subfolder.
    """
    if output_folder is None:
        output_folder = os.path.join(images_folder, "no_bg")
    
    # Create output folder if it doesn't exist
    os.makedirs(output_folder, exist_ok=True)
    
    # Supported image extensions
    image_extensions = ['*.jpg', '*.jpeg', '*.png', '*.bmp', '*.tiff', '*.tif']
    
    # Find all image files
    image_files = []
    for ext in image_extensions:
        image_files.extend(glob.glob(os.path.join(images_folder, ext)))
        image_files.extend(glob.glob(os.path.join(images_folder, ext.upper())))
    
    # Remove duplicates
    image_files = list(set(image_files))
    
    if not image_files:
        print(f"[rembg] No image files found in {images_folder}")
        return False
    
    print(f"[rembg] Found {len(image_files)} images to process...")
    
    success_count = 0
    # Process each image
    for i, input_path in enumerate(image_files):
        filename = os.path.basename(input_path)
        name, ext = os.path.splitext(filename)
        
        # Create output path
        output_path = os.path.join(output_folder, f"{name}_nobg{ext}")
        
        print(f"[rembg] Processing {i+1}/{len(image_files)}: {filename}")
        if remove_background(input_path, output_path):
            success_count += 1
    
    print(f"[rembg] Completed: {success_count}/{len(image_files)} images processed successfully")
    return success_count > 0

if __name__ == "__main__":
    input_image_path = "input.png"  # Replace with your input image file name
    output_image_path = "output_no_bg.png" # Replace with your desired output file name

    # Example usage:
    # Ensure you have an 'input.png' in the same directory or provide the full path
    remove_background(input_image_path, output_image_path)