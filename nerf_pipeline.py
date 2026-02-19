"""
NeRF reconstruction pipeline using Instant-NGP Python bindings (CORRECTED VERSION)
Integrates with CrossHatchEditor for real-time progress tracking

CRITICAL FIX: Use testbed.frame() not testbed.train() for proper training step sampling
"""
import sys
import json
import time
from pathlib import Path

# Add pyngp to path
pyngp_path = str(Path(__file__).parent / 'pyngp')
sys.path.insert(0, pyngp_path)

try:
    import pyngp as ngp
    import numpy as np
except ImportError as e:
    print(f"ERROR: Could not import pyngp from {pyngp_path}")
    print(f"Make sure pyngp.cp313-win_amd64.pyd exists in {pyngp_path}")
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
        testbed.training_batch_size = 1024
        
        # CRITICAL: Validate camera setup before training
        print(f"[pyngp] ⚠️  IMPORTANT: Camera diagnostics (quality indicator):")
        data_root = Path(images_dir)
        transforms_path = data_root / "transforms.json"
        if transforms_path.exists():
            with open(transforms_path, 'r') as f:
                transforms = json.load(f)
            
            frames = transforms.get('frames', [])
            if frames:
                # Extract camera positions
                positions = []
                for frame in frames:
                    mat = np.array(frame['transform_matrix'])
                    pos = mat[:3, 3]
                    positions.append(pos)
                
                positions = np.array(positions)
                spread = positions.std(axis=0)
                
                # Check if cameras are well-distributed
                print(f"[pyngp]   - Number of frames: {len(frames)}")
                print(f"[pyngp]   - Camera position std (should be >0.5 in each axis):")
                print(f"[pyngp]     * X: {spread[0]:.4f} {'✓' if spread[0] > 0.5 else '⚠️ LOW'}")
                print(f"[pyngp]     * Y: {spread[1]:.4f} {'✓' if spread[1] > 0.5 else '⚠️ LOW'}")
                print(f"[pyngp]     * Z: {spread[2]:.4f} {'✓' if spread[2] > 0.5 else '⚠️ CRITICAL - nearly collinear!'}")
                
                # Warn if cameras are too close
                if spread.min() < 0.1:
                    print(f"[pyngp]   ⚠️  WARNING: Cameras are nearly collinear!")
                    print(f"[pyngp]      This typically means COLMAP SfM failed or monocular video.")
                    print(f"[pyngp]      NeRF quality will be severely limited.")
                    print(f"[pyngp]      → Verify COLMAP 'sparse' folder has good reconstruction")
                    print(f"[pyngp]      → Check that cameras move around the object, not along it")
        
        print(f"\n[pyngp] Starting NeRF training for {n_steps} steps...")
        
        # Train using frame() method - CRITICAL FIX!
        # frame() properly samples training data for each step without "0 samples" warning
        start_step = 0
        for step in range(n_steps):
            # Use frame() for proper single training step with sampling
            testbed.frame()
            
            # Write progress to file (for C++ to read)
            if progress_file and step % max(1, n_steps // 100) == 0:  # ~100 updates total
                progress = int((step / max(1, n_steps)) * 100)
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
        
        final_loss = float(testbed.loss)
        print(f"[pyngp] Training complete. Final loss: {final_loss:.6f}")
        
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
        
        try:
            # Use numpy array for resolution parameter
            mesh_resolution = np.array([256, 256, 256], dtype=np.int32)
            testbed.compute_and_save_marching_cubes_mesh(
                mesh_path,
                mesh_resolution,
                thresh=2.5
            )
            # Check if mesh has content
            mesh_size = Path(mesh_path).stat().st_size if Path(mesh_path).exists() else 0
            if mesh_size > 100:
                print(f"[pyngp] Mesh exported successfully ({mesh_size / (1024*1024):.2f} MB)")
            else:
                print(f"[pyngp] ⚠️  Note: Mesh created but appears empty")
                print(f"[pyngp]    This often means COLMAP camera poses are unreliable.")
                print(f"[pyngp]    Try training for more steps or check COLMAP reconstruction quality.")
        except Exception as e:
            print(f"[pyngp] Warning: Mesh export failed: {e}")
        
        result = {
            'success': True,
            'model_path': output_path,
            'mesh_path': mesh_path,
            'final_loss': float(final_loss),
            'steps_completed': n_steps
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
    
    print(f"[pyngp] === Instant-NGP NeRF Pipeline (CORRECTED VERSION) ===")
    print(f"[pyngp] Images directory: {images_dir}")
    print(f"[pyngp] Output path: {output_path}")
    print(f"[pyngp] Steps: {n_steps}")
    if progress_file:
        print(f"[pyngp] Progress file: {progress_file}")
    print(f"[pyngp] =====================================================")
    
    result = train_nerf(images_dir, output_path, n_steps, progress_file)
    
    # Write final result
    result_file = str(Path(output_path).parent / "nerf_result.json")
    with open(result_file, 'w') as f:
        json.dump(result, f, indent=2)
    print(f"[pyngp] Result saved to: {result_file}")
    
    sys.exit(0 if result['success'] else 1)

if __name__ == '__main__':
    main()