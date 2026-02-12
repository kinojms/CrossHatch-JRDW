"""
NeRF reconstruction pipeline using Instant-NGP Python bindings
Integrates with CrossHatchEditor for real-time progress tracking
"""
import sys
import json
import time
from pathlib import Path
import os

# Debug: Print Python version and paths
print(f"[pyngp] Python version: {sys.version}")
print(f"[pyngp] Python executable: {sys.executable}")

# Add pyngp to path
pyngp_path = str(Path(__file__).parent / 'pyngp')
print(f"[pyngp] Looking for pyngp in: {pyngp_path}")
print(f"[pyngp] Directory exists: {os.path.exists(pyngp_path)}")
print(f"[pyngp] Files in {pyngp_path}:")
if os.path.exists(pyngp_path):
    for file in os.listdir(pyngp_path):
        print(f"  - {file}")

sys.path.insert(0, pyngp_path)
print(f"[pyngp] Added to sys.path: {pyngp_path}")
print(f"[pyngp] sys.path (first 5 entries):")
for i, p in enumerate(sys.path[:5]):
    print(f"  {i}: {p}")

try:
    print(f"[pyngp] Attempting to import pyngp...")
    import pyngp as ngp
    print(f"[pyngp] Successfully imported pyngp")
    import numpy as np
    print(f"[pyngp] Successfully imported numpy")
except ImportError as e:
    print(f"[pyngp] ERROR: Could not import pyngp from {pyngp_path}")
    print(f"[pyngp] ImportError details: {e}")
    print(f"[pyngp] Make sure pyngp.cp313-win_amd64.pyd exists in {pyngp_path}")
    sys.exit(1)

def train_nerf(images_dir, output_path, n_steps=10000, progress_file=None):
    """
    Train a NeRF model on images using Instant-NGP
    
    Args:
        images_dir: Directory containing images (must have transforms.json from COLMAP)
        output_path: Where to save the trained model (.ingp)
        n_steps: Number of training steps
        progress_file: Optional file path to write progress JSON (for UI updates)
    
    Returns:
        Dictionary with success status and model paths
    """
    try:
        print(f"[pyngp] Loading training data from: {images_dir}")
        
        # Create testbed
        testbed = ngp.Testbed(ngp.TestbedMode.Nerf)
        testbed.load_training_data(str(images_dir))
        testbed.shall_train = True
        
        print(f"[pyngp] Starting NeRF training for {n_steps} steps...")
        
        # Train
        step = 0
        for step in range(n_steps):
            testbed.train(batch_size=1024)
            
            # Write progress to file (for C++ to read)
            if progress_file and step % 100 == 0:
                progress = int((step / n_steps) * 100)
                loss = float(testbed.loss)
                progress_data = {
                    'step': step,
                    'total_steps': n_steps,
                    'progress': progress,
                    'loss': loss
                }
                try:
                    with open(progress_file, 'w') as f:
                        json.dump(progress_data, f)
                except Exception as e:
                    print(f"[pyngp] Warning: Could not write progress file: {e}")
        
        print(f"[pyngp] Training complete. Final loss: {testbed.loss}")
        
        # Render test image
        print(f"[pyngp] Rendering test image...")
        image = testbed.render(1920, 1080, spp=16, linear=True)
        
        # Save model
        output_path = str(output_path)
        print(f"[pyngp] Saving model to: {output_path}")
        testbed.save_snapshot(output_path, compress=True)
        
        # Export mesh (change .ingp to .obj)
        output_stem = Path(output_path).stem
        mesh_path = str(Path(output_path).parent / f"{output_stem}.obj")
        print(f"[pyngp] Exporting mesh to: {mesh_path}")
        testbed.compute_and_save_marching_cubes_mesh(
            mesh_path,
            resolution=256,
            thresh=2.5
        )
        
        result = {
            'success': True,
            'model_path': output_path,
            'mesh_path': mesh_path,
            'final_loss': float(testbed.loss),
            'steps_completed': step + 1
        }
        print(f"[pyngp] Success! Model saved to: {output_path}")
        print(f"[pyngp] Mesh saved to: {mesh_path}")
        return result
        
    except Exception as e:
        import traceback
        error_result = {
            'success': False,
            'error': str(e),
            'error_type': type(e).__name__,
            'traceback': traceback.format_exc()
        }
        print(f"[pyngp] Error: {error_result['error']}")
        return error_result

def main():
    """Command-line interface for the pipeline"""
    if len(sys.argv) < 3:
        print("Usage: python nerf_pipeline.py <images_dir> <output_path> [n_steps] [progress_file]")
        sys.exit(1)
    
    images_dir = sys.argv[1]
    output_path = sys.argv[2]
    n_steps = int(sys.argv[3]) if len(sys.argv) > 3 else 10000
    progress_file = sys.argv[4] if len(sys.argv) > 4 else None
    
    print(f"[pyngp] === Instant-NGP NeRF Pipeline ===")
    print(f"[pyngp] Images directory: {images_dir}")
    print(f"[pyngp] Output path: {output_path}")
    print(f"[pyngp] Steps: {n_steps}")
    if progress_file:
        print(f"[pyngp] Progress file: {progress_file}")
    print(f"[pyngp] ====================================")
    
    result = train_nerf(images_dir, output_path, n_steps, progress_file)
    
    # Write final result
    result_file = str(Path(output_path).parent / "nerf_result.json")
    with open(result_file, 'w') as f:
        json.dump(result, f, indent=2)
    print(f"[pyngp] Result saved to: {result_file}")
    
    sys.exit(0 if result['success'] else 1)

if __name__ == '__main__':
    main()