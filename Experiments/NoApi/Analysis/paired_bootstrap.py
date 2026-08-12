#!/usr/bin/env python3
"""Frozen decision statistic for M5.1 time metrics (spec section 8).

Consumes paired per-repetition medians and reports, per metric: the paired
percentage deltas relative to the incumbent, their median, the two-sided 95%
bootstrap confidence interval over the paired deltas (10,000 resamples, fixed
seed 0x4C4D5835), and the material verdict — material only if |median delta|
is at least 25% AND the interval excludes zero. The same rule applies to wins
and regressions; the sign says which one it is.

Input: JSON on stdin or as a file argument:
    {"metrics": {"<name>": {"pairs": [[incumbent, prototype], ...]}}}
Each pair is one repetition's median for both encoders (any consistent unit).
Exactly 12 pairs are expected per the spec; other counts are reported but
flagged, never silently accepted.

Output: one JSON object per run on stdout with per-metric results; exit 0.
This script takes no measurements and applies no judgment beyond the frozen
rule; gate evaluation and change-proportional judgment stay in Stage 5.
"""

import json
import random
import statistics
import sys

SEED = 0x4C4D5835
RESAMPLES = 10_000
EXPECTED_PAIRS = 12
MATERIAL_THRESHOLD_PCT = 25.0
CONFIDENCE = 0.95


def paired_deltas_pct(pairs: list[list[float]]) -> list[float]:
    """Percentage delta of each pair relative to the incumbent (positive = prototype cheaper)."""
    deltas = []
    for incumbent, prototype in pairs:
        if incumbent == 0:
            raise ValueError("incumbent median of zero cannot anchor a percentage delta")
        deltas.append((incumbent - prototype) / incumbent * 100.0)
    return deltas


def bootstrap_ci(deltas: list[float], rng: random.Random) -> tuple[float, float]:
    """Two-sided 95% percentile bootstrap interval over the median of the paired deltas."""
    medians = []
    n = len(deltas)
    for _ in range(RESAMPLES):
        sample = [deltas[rng.randrange(n)] for _ in range(n)]
        medians.append(statistics.median(sample))
    medians.sort()
    lo_index = int((1.0 - CONFIDENCE) / 2.0 * RESAMPLES)
    hi_index = RESAMPLES - 1 - lo_index
    return medians[lo_index], medians[hi_index]


def analyze(metrics: dict) -> dict:
    results = {}
    for name, metric in sorted(metrics.items()):
        pairs = metric["pairs"]
        deltas = paired_deltas_pct(pairs)
        rng = random.Random(SEED)
        lo, hi = bootstrap_ci(deltas, rng)
        median_delta = statistics.median(deltas)
        excludes_zero = (lo > 0.0 and hi > 0.0) or (lo < 0.0 and hi < 0.0)
        material = abs(median_delta) >= MATERIAL_THRESHOLD_PCT and excludes_zero
        results[name] = {
            "pairCount": len(pairs),
            "pairCountMatchesSpec": len(pairs) == EXPECTED_PAIRS,
            "pairedDeltasPct": [round(d, 4) for d in deltas],
            "medianDeltaPct": round(median_delta, 4),
            "ci95Pct": [round(lo, 4), round(hi, 4)],
            "ciExcludesZero": excludes_zero,
            "material": material,
            "direction": "win" if median_delta > 0 else ("regression" if median_delta < 0 else "tie"),
        }
    return results


def main() -> int:
    raw = open(sys.argv[1]).read() if len(sys.argv) > 1 else sys.stdin.read()
    data = json.loads(raw)
    print(json.dumps({"rule": {"materialThresholdPct": MATERIAL_THRESHOLD_PCT,
                               "confidence": CONFIDENCE,
                               "resamples": RESAMPLES,
                               "seed": SEED,
                               "expectedPairs": EXPECTED_PAIRS},
                      "metrics": analyze(data["metrics"])}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
