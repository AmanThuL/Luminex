# Matched temporal reconstruction comparison

The runner captures Raw, Native TAA/TAAU and MetalFX at identical simulation frames, camera poses,
jitter indices and fixed render extents. The scene's deterministic camera rail supplies the motion;
without a rail, the camera stays at its initial pose. Default output is 1280×720 with 32 unsaved
warmup frames followed by 120 saved frames at 60 Hz. `--scale` controls the common input scale.
Every capture stores its camera/settings/status manifest. A fallback, scale clamp, incomplete run or
misaligned frame refuses the report instead of silently comparing different conditions.

Use an isolated environment (Python 3.11 or 3.12):

```bash
python3 -m venv /tmp/luminex-compare-venv
/tmp/luminex-compare-venv/bin/pip install -r Tools/TemporalCompare/requirements.txt
xmake build App
xmake setup --san-miguel
/tmp/luminex-compare-venv/bin/python Tools/TemporalCompare/compare.py \
  --app build/macosx/arm64/release/App --scene san-miguel \
  --frames 120 --warmup 32 --scale 0.5 --output /tmp/san-miguel-half
```

Open `/tmp/san-miguel-half/index.html` directly. It loads sibling PNGs through ordinary local image
URLs; no server, network, CDN or video encoder is needed. Synchronized playback waits for all three
images, with frame selection and a shared nearest-neighbour crop at 1:1, 2×, 4× or 8×. Click an
overview to move the crop. Browser/display scaling may affect physical display pixels; 1:1 means one
source pixel per CSS pixel. The same crop magnification applies to all three columns.

The output directory must be new or empty. All BMPs, PNGs, capture logs, frame manifests, executable
and shader SHA-256 hashes and exact launch commands are retained. San Miguel runs also retain the
fetched asset's provenance and license metadata. To regenerate only the report:

```bash
/tmp/luminex-compare-venv/bin/python Tools/TemporalCompare/compare.py \
  --report-only --output /tmp/san-miguel-half
```

## Optional LDR-FLIP differences

[NVIDIA's official FLIP evaluator](https://github.com/NVlabs/flip) is isolated in
`requirements-flip.txt`, pinned to `flip-evaluator==1.7` with NumPy and Pillow. Its
[Python API](https://github.com/NVlabs/flip/blob/main/src/flip_evaluator/flip_python_api.py) accepts
sRGB LDR images and the pixels-per-degree parameter. Install the optional requirements and add
`--flip`; `--ppd` defaults explicitly to 67.

```bash
/tmp/luminex-compare-venv/bin/pip install -r Tools/TemporalCompare/requirements-flip.txt
/tmp/luminex-compare-venv/bin/python Tools/TemporalCompare/compare.py \
  --report-only --flip --ppd 67 --output /tmp/san-miguel-half
```

Maps and statistics compare each Raw/MetalFX frame with the **matching Native TAA frame as an
algorithm baseline, not ground truth**. They measure differences, not accuracy, quality rank or a
pass/fail threshold. Maps use one fixed grayscale 0–1 scale, black meaning equal. Mean is the
arithmetic mean of full-frame per-pixel LDR-FLIP values; p95 is the 95th percentile with NumPy linear
interpolation. Crop controls never change these full-frame statistics. Package versions, viewing
assumptions, individual frame statistics and maps are stored in `comparison.json`.

## Validation

```bash
/tmp/luminex-compare-venv/bin/python -m unittest discover -s Tools/TemporalCompare -p 'test_*.py'
```

The App interface is `--capture-sequence <directory> --frames N --warmup W`. A sequence saves
simulation frames W through W+N−1 at frame/60 seconds; numbered filenames start at zero. Existing
`--screenshot` behavior still saves only the last of N rendered frames. Both flags together are an
error, and `--warmup` requires a sequence. A flat image or requested-vendor fallback leaves an
incomplete manifest and returns failure. Capture is for repeatability and image inspection, not a
GPU throughput benchmark: every frame waits for readback.
