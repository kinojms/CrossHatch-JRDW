import sys, os, subprocess
from pathlib import Path

# Auto-install rembg + pillow if missing
try:
    from rembg import remove
    from PIL import Image
except ModuleNotFoundError:
    print("rembg or pillow not found, installing...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "rembg", "pillow"])
    from rembg import remove
    from PIL import Image

def remove_backgrounds(folder):
    input_dir = Path(folder)
    output_dir = input_dir / "no_bg"
    output_dir.mkdir(exist_ok=True)

    for file in input_dir.glob("*.png"):
        out_path = output_dir / file.name
        try:
            with Image.open(file) as img:
                result = remove(img)
                result.save(out_path)
                print(f"Processed: {file.name}")
        except Exception as e:
            print(f"Error processing {file}: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python remove_bg_batch.py <folder_path>")
        sys.exit(1)
    remove_backgrounds(sys.argv[1])