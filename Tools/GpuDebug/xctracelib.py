"""Parse an `xcrun xctrace export` XML document into per-encoder GPU timing aggregates, and check
the result for the same class of anomaly anomalylib.py reports for a `.gputrace` dump.

The following constraints were verified against Xcode 16.0/17E202:

1. **Table name.** `xcrun xctrace export --input <trace> --toc` exposes the per-encoder interval
   table as `metal-application-encoders-list` (`ENCODER_TABLE_SCHEMA` below). Its exported rows
   carry start, duration, and label columns, including project-owned `lmx.` labels.
2. **Encoder-label granularity.** Render-pass labels originate in `rhi::RenderPassDesc` and are
   copied onto the Metal encoder. Shadow, scene, and UI therefore produce distinct buckets.
   Multiple invocations carrying the same label intentionally aggregate; this module does not
   reconstruct frame or draw boundaries.

Ref/id interning: xctrace's XML export interns repeated values -- the first occurrence of a value
carries `id="N"`, and every later occurrence of the identical value is written as an empty
`<tag ref="N"/>` instead of repeating the content. `resolve_refs` builds an id -> element map and
replaces every `ref`-bearing element with a deep copy of its target, so downstream code never has
to know interning happened.

Run: python3 -m unittest discover -s Tools/GpuDebug/tests -v

Note on `xml.etree.ElementTree`: stdlib's XML parsers are XXE/billion-laughs-vulnerable against
*untrusted* input, and this module deliberately uses it anyway rather than a hardened third-party
parser (e.g. defusedxml) -- the project's Python tools are stdlib-only, and the two inputs
here are hand-authored test fixtures and `xcrun xctrace export` output from a subprocess *this
same CLI* just launched locally, never XML fetched from a network or supplied by an untrusted
party.
"""
import collections
import copy
import math
import xml.etree.ElementTree as ET

# Xcode's exported per-encoder interval table.
ENCODER_TABLE_SCHEMA = "metal-application-encoders-list"

# Every Luminex-owned Metal object is
# labelled "lmx.<something>". A row whose encoder-label does not contain this substring belongs to
# something else entirely (the OS compositor, another process the trace also saw, ...).
LMX_LABEL_MARKER = "lmx."

# check_anomalies's unstable-pass rule fires only at or above this many samples for a label. Below
# it, a single slow outlier can push p95 past 2x p50 on pure noise -- not enough data to call that
# a real instability rather than the profiler catching one unlucky frame.
MIN_SAMPLES_FOR_UNSTABLE = 8

# The ratio itself: p95 more than this many times p50 counts as "unstable" (given the sample floor
# above).
UNSTABLE_RATIO = 2.0

# aggregate()'s optional frame-total entry is stored under this key rather than a real label, so
# it can never collide with one -- real encoder labels always contain LMX_LABEL_MARKER, and this
# key deliberately does not.
FRAME_TOTAL_KEY = "__frameTotalMsApprox__"

# resolve_refs resolves to a fixed point (bounded) rather than a single pass: nothing observed in
# a real capture nests a ref inside another id= definition, but nothing rules it out for a future
# Xcode either, and a single pass would silently under-resolve that case rather than fail loudly.
_MAX_RESOLVE_PASSES = 10

Interval = collections.namedtuple("Interval", ["label", "start_ns", "duration_ns"])


# --- resolve_refs --------------------------------------------------------------------------

def resolve_refs(xml_text: str) -> ET.ElementTree:
    """Resolve xctrace's `id=`/`ref=` value interning in an exported XML document.

    Every element carrying a `ref="N"` attribute is replaced in place with a deep copy of the
    element that carries `id="N"` (its content, its own `fmt`/other attributes, and its
    subtree). Raises ValueError if a `ref` names no known `id`, or if resolution does not
    converge within `_MAX_RESOLVE_PASSES` (a malformed or cyclic document).
    """
    root = ET.fromstring(xml_text)

    for _ in range(_MAX_RESOLVE_PASSES):
        id_map = {el.get("id"): el for el in root.iter() if el.get("id") is not None}
        parent_map = {child: parent for parent in root.iter() for child in parent}
        pending = [el for el in root.iter() if el.get("ref") is not None]
        if not pending:
            break
        for element in pending:
            ref_id = element.get("ref")
            target = id_map.get(ref_id)
            if target is None:
                raise ValueError(f"xctrace XML: ref={ref_id!r} has no matching id= element")
            parent = parent_map[element]
            index = list(parent).index(element)
            parent[index] = copy.deepcopy(target)
    else:
        raise ValueError("xctrace XML: ref resolution did not converge (possible ref cycle)")

    return ET.ElementTree(root)


# --- encoder_intervals -----------------------------------------------------------------------

def _schema_column_mnemonics(root: ET.Element) -> list:
    """The `<col><mnemonic>...</mnemonic>...</col>` order from the exported table's `<schema>`,
    which is also the order each `<row>`'s children appear in -- xctrace does not tag row
    children with their column name, so this positional order is the only way to know which
    child is which column."""
    schema = root.find(".//schema")
    if schema is None:
        return []
    return [col.findtext("mnemonic") for col in schema.findall("col")]


def encoder_intervals(resolved_tree) -> list:
    """Every `<row>` in a resolved xctrace export, as an `Interval(label, start_ns, duration_ns)`.

    `resolved_tree` is whatever `resolve_refs` returned (an ElementTree; a bare Element also
    works). Rows missing a start/duration/label column, or whose start/duration text is not an
    integer, are skipped rather than raising -- the same "degrade, don't crash" posture
    gputrace_dump.py takes with a single bad blob, since a schema drift in ONE column should not
    lose every other row's data.
    """
    root = resolved_tree.getroot() if hasattr(resolved_tree, "getroot") else resolved_tree
    columns = _schema_column_mnemonics(root)

    intervals = []
    for row in root.iter("row"):
        by_mnemonic = dict(zip(columns, row))
        start_el = by_mnemonic.get("start")
        duration_el = by_mnemonic.get("duration")
        label_el = by_mnemonic.get("encoder-label")
        if start_el is None or duration_el is None or label_el is None:
            continue
        try:
            start_ns = int(start_el.text)
            duration_ns = int(duration_el.text)
        except (TypeError, ValueError):
            continue
        intervals.append(Interval(label=label_el.text or "", start_ns=start_ns,
                                  duration_ns=duration_ns))
    return intervals


# --- aggregate ---------------------------------------------------------------------------------

def _nearest_rank(sorted_values: list, percentile: float):
    """Nearest-rank percentile: rank = ceil(p/100 * n), a 1-based index into the ascending-sorted
    list (clamped to [1, n]). For durations [1ms, 3ms] (n=2): p50 -> rank ceil(1.0)=1 -> 1ms;
    p95 -> rank ceil(1.9)=2 -> 3ms -- pinned by test_aggregate_math_for_the_two_sample_label."""
    n = len(sorted_values)
    rank = max(1, min(n, math.ceil(percentile / 100 * n)))
    return sorted_values[rank - 1]


def _ms(nanoseconds) -> float:
    return round(nanoseconds / 1_000_000, 3)


def aggregate(intervals: list, frame_marker_label: str = None) -> dict:
    """Per-label GPU timing stats: `{label: {"count", "totalMs", "meanMs", "p50Ms", "p95Ms",
    "maxMs"}}` for every unique label in `intervals`.

    `frame_marker_label`, when given and present among `intervals`, additionally computes an
    approximate per-frame GPU total -- (sum of every label's totalMs) / (frame_marker_label's own
    count) -- stored under `FRAME_TOTAL_KEY` rather than a real label. This assumes
    `frame_marker_label` names an encoder that runs exactly once per frame; today no Luminex
    encoder label satisfies that (see the module docstring's deviation note 2), so profile.py's
    default flow does not use this parameter and instead divides by the requested frame count
    directly -- it is exercised here as a general-purpose library feature for a caller that does
    have a reliable one-per-frame marker.
    """
    durations_by_label = collections.defaultdict(list)
    for interval in intervals:
        durations_by_label[interval.label].append(interval.duration_ns)

    result = {}
    for label, durations_ns in durations_by_label.items():
        durations_ns = sorted(durations_ns)
        total_ns = sum(durations_ns)
        result[label] = {
            "count": len(durations_ns),
            "totalMs": _ms(total_ns),
            "meanMs": _ms(total_ns / len(durations_ns)),
            "p50Ms": _ms(_nearest_rank(durations_ns, 50)),
            "p95Ms": _ms(_nearest_rank(durations_ns, 95)),
            "maxMs": _ms(durations_ns[-1]),
        }

    if frame_marker_label is not None and frame_marker_label in result:
        frame_count = result[frame_marker_label]["count"]
        if frame_count > 0:
            total_all_ms = sum(stats["totalMs"] for stats in result.values())
            result[FRAME_TOTAL_KEY] = round(total_all_ms / frame_count, 3)

    return result


# --- check_anomalies -----------------------------------------------------------------------

def _anomaly(check: str, severity: str, subject: str, finding: str, hint: str) -> dict:
    # Matches anomalylib._anomaly's shape for consistency across the two tools' outputs.
    return {"check": check, "severity": severity, "subject": subject, "finding": finding,
            "next_steps_hint": hint}


def _label_aggregates(aggregate_result: dict) -> dict:
    """`aggregate_result` minus FRAME_TOTAL_KEY (a plain float, not a per-label stats dict)."""
    return {label: stats for label, stats in aggregate_result.items() if label != FRAME_TOTAL_KEY}


def check_anomalies(aggregate_result: dict) -> list:
    """The two checks named in the plan: `no-encoders-matched` (error) when zero labels in
    `aggregate_result` contain `LMX_LABEL_MARKER`, else `unstable-pass` (warning) per label when
    p95Ms > UNSTABLE_RATIO x p50Ms *and* count >= MIN_SAMPLES_FOR_UNSTABLE (see that constant's
    comment for why the sample floor exists). Deterministic order: labels sorted, matching
    anomalylib's "subjects sort by label" convention.
    """
    labelled = _label_aggregates(aggregate_result)
    lmx_labelled = {label: stats for label, stats in labelled.items() if LMX_LABEL_MARKER in label}

    if not lmx_labelled:
        observed = sorted(labelled)
        seen = f" (saw: {', '.join(observed)})" if observed else " (no encoder rows were extracted at all)"
        return [_anomaly(
            "no-encoders-matched", "error", "encoders",
            f"0 of {len(observed)} observed encoder label(s) contain {LMX_LABEL_MARKER!r}{seen}",
            "check the right App binary/scene was profiled, and that its render passes still "
            "label their command encoders with an 'lmx.' prefix "
            "(Source/RHI/Metal4/Metal4CommandList.cpp)")]

    anomalies = []
    for label in sorted(lmx_labelled):
        stats = lmx_labelled[label]
        if stats["count"] < MIN_SAMPLES_FOR_UNSTABLE:
            continue
        if stats["p95Ms"] > UNSTABLE_RATIO * stats["p50Ms"]:
            anomalies.append(_anomaly(
                "unstable-pass", "warning", label,
                f"p95 ({stats['p95Ms']}ms) is more than {UNSTABLE_RATIO:g}x p50 "
                f"({stats['p50Ms']}ms) over {stats['count']} samples",
                "GPU time for this encoder label varies a lot frame to frame -- check for "
                "input-dependent draw/item counts, thermal throttling, or contention with other "
                "GPU work (and remember one label may cover more than one pass -- see this "
                "module's docstring, deviation note 2)"))
    return anomalies
