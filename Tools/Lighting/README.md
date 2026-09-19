# Local-light evidence tools

`python3 Tools/Lighting/missed_oracle.py --input capture-directory --out new-report.json`
counts exact black, red, yellow and other RGB pixels from a native Missed-view PNG capture.
Red is missing-light failure. Yellow is overflow; `--allow-yellow` permits it only for saturation
fixtures. Unexpected colours, transparency, missing frames and malformed files remain harness
failures. Both failure classes survive together. `--selftest` requires only Python's standard library;
Pillow accelerates full captures when available.

`python3 Tools/Bench/lighting_paired.py --binary /frozen/candidate/App --out /new/local`
collects Direct/Clustered diagnostics for LightLab 64/256/1024/4096 and the Sponza rig.
`--control zero --parent /frozen/parent/App` compares the six frozen visibility workloads with
candidate local lighting Off. The parent remains schema 3; the candidate must be schema 4.
Every run fixes Native TAA at 1280×720, scale 1, W32/N256. Twelve fresh-process pairs alternate
AB/BA; paired medians use 10,000 bootstrap resamples with seed `0x4C4D5836`.
`--workloads` can select a subset, which does not constitute the complete validation inventory.

Keep each executable with its runtime shader tree. The driver refuses reused output directories,
retains raw JSON and process receipts for failed attempts, verifies executable/shader/host provenance,
and reports all cells without choosing a default. Schema 4 requires a lighting observation joined
to each sample's exact device frame, including Off/Direct/zero-light frames. `lightingGpuMs` is
recomputed from raw `lmx.pass.light.*` timings, separately from scene and full timed-pass sums.
List bytes are the retired assigned prefix; allocated bytes cover the three paced index buffers.
All costs are serialized-retirement diagnostics, not throughput or an adoption threshold.
Do not collect costs while builds, tests, or other GPU workloads run.
