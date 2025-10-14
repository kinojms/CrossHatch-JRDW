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

def remove_backgrounds(folder, file):
    input_dir = Path(folder)
    output_dir = input_dir / "no_bg"
    output_dir.mkdir(exist_ok=True)
    processed = 0

    out_path = output_dir / file
    try:
        with Image.open(file) as img:
            result = remove(img)
            result.save(out_path)
            processed += 1
            print(f"Processed: {file}")

    except Exception as e:
        print(f"Error processing {file}: {e}")
        # even if error, count progress to avoid stuck bar
        processed += 1

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python remove_bg_batch.py <path> / <filename>")
        sys.exit(1)
    remove_backgrounds(sys.argv[1], sys.argv[2])
