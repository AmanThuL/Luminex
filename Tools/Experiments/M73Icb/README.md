# Indexed ICB spike

Experimental source for the frozen M7.3 feasibility gate. Read
`docs/research/2026-09-15-m7.3-icb-runtime-spike.md` for results and limits.

`compile_probes.py` reproduces the original compiler matrix. `run_runtime.py` builds the standalone
Metal 4 runtime against operator-supplied pinned Slang and metal-cpp paths, then runs it under
validation. Use `--build-only` when another task owns the GPU. Output must be outside the source
checkout. The runtime is a bounded diagnostic, not an application or a production adapter.

The complete gate remains unresolved because the exercised ICB route could not produce the
required labelled capture. Native indexed execution, two-material texture parity and 900-frame
retirement passed; the fixed-command capture control also passed.
