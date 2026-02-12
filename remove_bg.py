import sys, os, subprocess
from pathlib import Path
import gc

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

# Auto-install rembg + pillow if missing
try:
    from rembg import remove
    from PIL import Image
except ModuleNotFoundError:
    print("[rembg] Installing rembg and pillow...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "rembg", "pillow", "-q"])
    from rembg import remove
    from PIL import Image

def remove_backgrounds(folder, file):
    """Remove background from a single image file.
    
    Args:
        folder: Output directory where 'no_bg' subfolder will be created
        file: Full path to the input image file
    """
    try:
        input_path = Path(file)
        if not input_path.exists():
            print(f"Error processing {file}: File not found")
            return False
            
        output_dir = Path(folder) / "no_bg"
        output_dir.mkdir(exist_ok=True, parents=True)
        
        out_path = output_dir / input_path.name
        
        # Load image and remove background
        with Image.open(input_path) as img:
            result = remove(img)
            result.save(out_path)
            print(f"Processed: {input_path.name}")
            
            # Force garbage collection after each image to reduce memory usage
            gc.collect()
            return True

    except Exception as e:
        print(f"Error processing {file}: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python remove_bg_batch.py <path> / <filename>")
        sys.exit(1)
    remove_backgrounds(sys.argv[1], sys.argv[2])
