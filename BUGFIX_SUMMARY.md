# Background Removal & NeRF Pipeline Bug Fixes

## Issues Fixed

### 1. **Hardcoded Path Error** ❌ → ✅
**Problem**: Line 99 in `Reconstructor.cpp` referenced a hardcoded path to `colmap2nerf.py` on the E: drive that doesn't exist on your system:
```
E:\Development\School_Projects\Thesis\instant-ngp-2000\scripts\colmap2nerf.py
```

**Solution**: 
- Removed the hardcoded developer path
- Disabled the COLMAP automation (commented out)
- Users should prepare their own `transforms.json` using COLMAP separately, or place `colmap2nerf.py` in the project root

### 2. **Numba JIT Cache Corruption** ❌ → ✅
**Problem**: The error `OSError: [WinError -529697949] Windows Error 0xe06d7363` was caused by corrupted Numba JIT compilation cache files. This happened in `pymatting.alpha.estimate_alpha_sm` module.

**Solution** (in `remove_bg.py`):
- Added automatic Numba cache clearing on startup
- Disabled Numba parallelization: `NUMBA_NUM_THREADS=1`
- Create isolated cache directory to prevent conflicts: `.numba_cache/`

### 3. **File Path Bug** ❌ → ✅
**Problem**: `remove_bg.py` wasn't constructing the full file path correctly:
```python
out_path = output_dir / file  # Bug: file is just filename, should include full path
with Image.open(file) as img:  # Bug: tries to open relative path that doesn't exist
```

**Solution**:
- Changed to properly use `Path(file)` for the input
- Verify file exists before processing
- Construct output path using `input_path.name`

### 4. **ONNX Runtime Memory Allocation Failures** ❌ → ✅
**Problem**: Errors like:
```
Exception during initialization: bad allocation
Non-zero status code returned while running Concat node: Failed to allocate memory
```

**Root Cause**: Running 5 parallel threads trying to load the ONNX background removal model simultaneously caused memory exhaustion.

**Solution** (in `Reconstructor.cpp`):
- Reduced parallel threads from 5 to 2: `std::min(2, totalFiles)`
- Added 500ms stagger between thread launches to avoid simultaneous model loading
- Each thread now has time to initialize before the next starts

### 5. **Memory Management** ❌ → ✅
**Problem**: ONNX Runtime was running out of memory while processing background removal.

**Solution** (in `remove_bg.py`):
- Added `gc.collect()` after each image to force garbage collection
- Reduces memory footprint per-thread significantly

## Files Modified

1. **Reconstructor.cpp**
   - Removed hardcoded `kColmapScriptPath` 
   - Disabled COLMAP automation
   - Changed max threads from 5 to 2
   - Added staggered thread launch with 500ms delays

2. **remove_bg.py**
   - Added Numba cache clearing
   - Disabled Numba parallelization
   - Fixed file path handling
   - Added garbage collection per image
   - Improved error handling and tracing

## Testing Recommendations

1. **Clear Python cache** before testing:
   ```bash
   # Windows
   rmdir /s /q %USERPROFILE%\.numba_cache
   rmdir /s /q %USERPROFILE%\.cache\pip
   ```

2. **Upgrade rembg and dependencies** if issues persist:
   ```bash
   python -m pip install --upgrade rembg pymatting numba llvmlite
   ```

3. **Monitor memory usage** during background removal using Task Manager

4. **For COLMAP support**: Place `colmap2nerf.py` in the project root or set up a proper instant-ngp installation path if you want to re-enable COLMAP automation

## Environment Variables Set

The `remove_bg.py` script now sets:
- `NUMBA_NUM_THREADS=1` - Single-threaded Numba compilation
- `NUMBA_CACHE_DIR=./.numba_cache` - Isolated cache to avoid conflicts
