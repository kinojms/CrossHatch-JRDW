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

        # Locate transforms.json: try images_dir, then parent, then parent.parent
        transforms_path = None
        images_candidate = Path(images_dir)
        candidates = [images_candidate, images_candidate.parent, images_candidate.parent.parent]
        for cand in candidates:
            p = cand / "transforms.json"
            print(f"[pyngp] Checking for transforms.json at: {p}")
            if p.exists():
                transforms_path = p
                images_dir = str(cand)
                print(f"[pyngp] Found transforms.json at: {transforms_path}")
                break

        if transforms_path is None:
            print(f"[pyngp] Warning: transforms.json not found in images dir or parents; proceeding with provided images_dir")
        else:
            with open(transforms_path, 'r') as f:
                transforms = json.load(f)
            print(f"[pyngp] transforms.json has {len(transforms.get('frames', []))} frames")
            for i, frame in enumerate(transforms.get('frames', [])[:3]):
                fp = frame.get('file_path', '')
                print(f"[pyngp] Frame {i} path: {fp} (absolute={Path(fp).is_absolute()}, exists={Path(fp).exists()})")
            
            # Fix: Instant-NGP uses camera_angle_x/y to compute focal lengths if present,
            # but also respects fl_x/fl_y if they're in the right scale.
            # The issue: we had fl_x/fl_y in pixel units, and they were normalized to 1.4/0.8,
            # which is still wrong. Let's use camera_angle_x to compute the correct normalized focal length.
            import math
            w = transforms.get('w', 1080)
            h = transforms.get('h', 1920)
            cam_angle_x = transforms.get('camera_angle_x', None)
            cam_angle_y = transforms.get('camera_angle_y', None)
            fl_x = transforms.get('fl_x', None)
            fl_y = transforms.get('fl_y', None)
            
            print(f"[pyngp] Original camera intrinsics: fl_x={fl_x}, fl_y={fl_y}, w={w}, h={h}")
            print(f"[pyngp] Field of view angles: camera_angle_x={cam_angle_x}, camera_angle_y={cam_angle_y}")
            
            # If camera_angle_x is present, compute focal length from it.
            # FOV relationship: tan(angle/2) = (image_size/2) / focal_length
            # So: focal_length = (image_size/2) / tan(angle/2)
            if cam_angle_x is not None:
                fl_x_from_angle = (w / 2.0) / math.tan(cam_angle_x / 2.0)
                print(f"[pyngp] Computed fl_x from camera_angle_x: {fl_x_from_angle} pixels")
                # For Instant-NGP: normalize by image width to get relative focal length
                fl_x_normalized = fl_x_from_angle / w
                transforms['fl_x'] = fl_x_normalized
                print(f"[pyngp] Set fl_x to normalized: {fl_x_normalized}")
            
            if cam_angle_y is not None:
                fl_y_from_angle = (h / 2.0) / math.tan(cam_angle_y / 2.0)
                print(f"[pyngp] Computed fl_y from camera_angle_y: {fl_y_from_angle} pixels")
                fl_y_normalized = fl_y_from_angle / h
                transforms['fl_y'] = fl_y_normalized
                print(f"[pyngp] Set fl_y to normalized: {fl_y_normalized}")
            
            # Ensure principal point is in the right range
            # Check if already normalized (values < 10 suggest pixel space)
            cx = transforms.get('cx', w / 2)
            cy = transforms.get('cy', h / 2)
            if cx > 10 or cy > 10:
                # In pixel coordinates, normalize to [0, 1]
                transforms['cx'] = cx / w
                transforms['cy'] = cy / h
                print(f"[pyngp] Converted principal point from pixels: ({cx}, {cy}) → ({transforms['cx']}, {transforms['cy']})")
            else:
                # Already normalized, ensure it makes sense (should be near 0.5 for center)
                print(f"[pyngp] Principal point already normalized: ({cx}, {cy})")
            
            # Remove camera_angle_x/y if they're causing confusion with fl_x/fl_y
            # (some versions of Instant-NGP may not handle both correctly)
            if 'camera_angle_x' in transforms:
                del transforms['camera_angle_x']
            if 'camera_angle_y' in transforms:
                del transforms['camera_angle_y']
            print(f"[pyngp] Removed camera_angle_x/y from transforms to avoid confusion")
            
            # Write corrected transforms back to disk
            with open(transforms_path, 'w') as f:
                json.dump(transforms, f, indent=2)
            print(f"[pyngp] Wrote corrected transforms to {transforms_path}")

        # Create testbed
        testbed = ngp.Testbed(ngp.TestbedMode.Nerf)
        
        # Ensure a `base.json` network config exists near the dataset
        def ensure_base_json(dst_folder: Path):
            base_candidate = dst_folder / 'base.json'
            if base_candidate.exists():
                return True
            inst_root = os.environ.get('INSTANT_NGP_PATH')
            candidates = []
            if inst_root:
                candidates.append(Path(inst_root) / 'configs' / 'nerf' / 'base.json')
            candidates.append(Path('D:/Thesis/repos/instant-ngp') / 'configs' / 'nerf' / 'base.json')
            candidates.append(Path(__file__).resolve().parent.parent / 'instant-ngp' / 'configs' / 'nerf' / 'base.json')
            for c in candidates:
                try:
                    if c.exists():
                        print(f"[pyngp] Copying base.json from {c}")
                        base_candidate.write_text(c.read_text())
                        return True
                except Exception:
                    continue
            print(f"[pyngp] base.json not found in known locations")
            return False

        ensure_base_json(Path(images_dir))

        # If a previous nerf_result.json exists (from failed runs), move it aside to avoid confusing loader
        try:
            nr_path = Path(images_dir) / 'nerf_result.json'
            if nr_path.exists():
                backup = nr_path.with_suffix('.json.bak')
                print(f"[pyngp] Backing up existing nerf_result.json to {backup}")
                nr_path.rename(backup)
        except Exception as e:
            print(f"[pyngp] Warning: could not backup nerf_result.json: {e}")

        testbed.load_training_data(str(images_dir))
        
        # After loading data, check if we need to set camera matrix directly
        # This can help if the transforms.json camera intrinsics are misinterpreted
        try:
            # Try to get the current camera matrix and print diagnostics
            if hasattr(testbed, 'camera_matrix'):
                cam_mat = testbed.camera_matrix
                print(f"[pyngp] After load_training_data, camera_matrix shape: {cam_mat.shape if hasattr(cam_mat, 'shape') else type(cam_mat)}")
            if hasattr(testbed, 'relative_focal_length'):
                rfl = testbed.relative_focal_length
                print(f"[pyngp] After load_training_data, relative_focal_length: {rfl}")
        except Exception as e:
            print(f"[pyngp] Warning: could not inspect camera properties: {e}")
        # Ensure a clean start
        try:
            testbed.reset()
        except Exception:
            pass

        # If a base.json was copied into the dataset folder, ask the testbed to load it
        try:
            base_json_path = Path(images_dir) / 'base.json'
            if base_json_path.exists():
                print(f"[pyngp] Reloading network from file: {base_json_path}")
                try:
                    testbed.reload_network_from_file(str(base_json_path))
                    print(f"[pyngp] Successfully reloaded network from {base_json_path}")
                except Exception as e:
                    print(f"[pyngp] reload_network_from_file failed: {e}")
            else:
                print(f"[pyngp] No base.json to reload at {base_json_path}")
        except Exception as e:
            print(f"[pyngp] Warning while attempting to reload base.json: {e}")
        # Set safe training defaults
        try:
            testbed.training_batch_size = 1024
        except Exception:
            pass
        try:
            testbed.training_step = 0
        except Exception:
            pass
        testbed.shall_train = True
        
        print(f"[pyngp] Starting NeRF training for {n_steps} steps...")

        # Train
        step = 0
        # Print some diagnostics about the testbed object to help troubleshoot pyngp errors
        try:
            print(f"[pyngp] testbed dir(): {', '.join(sorted([k for k in dir(testbed) if not k.startswith('_')]) )}")
        except Exception:
            print(f"[pyngp] Warning: unable to list testbed attributes")

        # Print root_dir and check for config files that instant-ngp may expect
        try:
            cwd = os.getcwd()
            print(f"[pyngp] cwd: {cwd}")
        except Exception:
            pass
        try:
            root_dir = getattr(testbed, 'root_dir', None)
            print(f"[pyngp] testbed.root_dir: {root_dir}")
        except Exception:
            print(f"[pyngp] Warning: could not read testbed.root_dir")

        # Check for base.json and nerf_result.json in a few likely places
        check_paths = [Path(images_dir), Path(images_dir).parent, Path(images_dir).parent.parent, Path.cwd()]
        for p in check_paths:
            base = p / 'base.json'
            nr = p / 'nerf_result.json'
            if base.exists():
                try:
                    print(f"[pyngp] Found base.json at {base}; contents:\n{base.read_text()}")
                except Exception as e:
                    print(f"[pyngp] Could not read base.json at {base}: {e}")
            if nr.exists():
                try:
                    print(f"[pyngp] Found nerf_result.json at {nr}; contents snippet:\n{nr.read_text()[:1000]}")
                except Exception as e:
                    print(f"[pyngp] Could not read nerf_result.json at {nr}: {e}")

        for step in range(n_steps):
            try:
                # pyngp.Testbed.train expects a single positional integer (number of steps to run)
                testbed.train(1)
            except Exception as e:
                print(f"[pyngp] Exception during testbed.train at step {step}: {e}")
                # Attempt to capture additional testbed state if possible
                try:
                    if hasattr(testbed, 'nerf'):
                        print(f"[pyngp] testbed.nerf attributes: {', '.join([k for k in dir(testbed.nerf) if not k.startswith('_')])}")
                except Exception:
                    pass
                raise
            
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
        try:
            # Most pyngp builds expect a 3-element resolution vector
            testbed.compute_and_save_marching_cubes_mesh(
                mesh_path,
                resolution=[256, 256, 256],
                thresh=2.5
            )
        except TypeError as e:
            print(f"[pyngp] compute_and_save_marching_cubes_mesh TypeError: {e}; retrying with positional args")
            try:
                testbed.compute_and_save_marching_cubes_mesh(mesh_path, [256, 256, 256], 2.5)
            except Exception as e2:
                print(f"[pyngp] Mesh export retry failed: {e2}")
                raise
        
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