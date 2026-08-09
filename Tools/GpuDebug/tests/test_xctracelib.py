"""Tests for xctracelib.py -- xctrace export XML ref-resolution, encoder-interval extraction,
aggregation math, and the anomaly checks over the aggregate.

Driven by two hand-written fixtures under tests/fixtures/ (xctrace-sample.xml, xctrace-no-lmx.xml)
rather than a real capture: a real `xcrun xctrace record` needs a display and Xcode and takes
minutes, so these fixtures pin the parsing and aggregation logic deterministically. Real-trace
verification belongs to the manual profiling check.

Run: python3 -m unittest discover -s Tools/GpuDebug/tests -v
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

import xctracelib  # noqa: E402

_FIXTURES = pathlib.Path(__file__).resolve().parent / "fixtures"

_SHADOW = "lmx.render.shadowPass"
_SCENE = "lmx.render.scenePass"


def _load(name: str) -> str:
    return (_FIXTURES / name).read_text()


def _sample_intervals():
    tree = xctracelib.resolve_refs(_load("xctrace-sample.xml"))
    return xctracelib.encoder_intervals(tree)


# --- resolve_refs --------------------------------------------------------------------------

class ResolveRefsTests(unittest.TestCase):
    def test_every_ref_is_replaced_with_a_deep_copy_of_its_id_target(self):
        tree = xctracelib.resolve_refs(_load("xctrace-sample.xml"))
        root = tree.getroot()

        rows = root.findall(".//row")
        self.assertEqual(len(rows), 3)
        for row in rows:
            for child in row:
                self.assertIsNone(child.get("ref"),
                                  f"<{child.tag} ref={child.get('ref')!r}> was not resolved")

    def test_resolved_row_children_carry_the_targets_actual_values(self):
        tree = xctracelib.resolve_refs(_load("xctrace-sample.xml"))
        rows = tree.getroot().findall(".//row")

        # Row 2 and row 3 both `ref` row 1's <start-time id="1">1000000</start-time>.
        for row in (rows[1], rows[2]):
            start = row.find("start-time")
            self.assertEqual(start.text, "1000000")

        # Row 2 also `ref`s row 1's <metal-object-label id="3"> (the shadowPass label).
        label = rows[1].findall("metal-object-label")[0]
        self.assertEqual(label.text, "lmx.render.shadowPass")

    def test_unresolved_ref_raises(self):
        xml_text = """<trace-query-result><node>
            <row><start-time ref="999"/></row>
        </node></trace-query-result>"""
        with self.assertRaises(ValueError):
            xctracelib.resolve_refs(xml_text)


# --- encoder_intervals -----------------------------------------------------------------------

class EncoderIntervalsTests(unittest.TestCase):
    def test_extracts_label_start_and_duration_for_every_row(self):
        intervals = _sample_intervals()

        self.assertEqual(len(intervals), 3)
        labels = [iv.label for iv in intervals]
        self.assertEqual(labels, [_SHADOW, _SHADOW, _SCENE])

    def test_start_and_duration_are_nanoseconds_ints(self):
        intervals = _sample_intervals()

        first = intervals[0]
        self.assertEqual(first.start_ns, 1000000)
        self.assertEqual(first.duration_ns, 1000000)
        # Row 3 reuses row 1's interned start via ref=, so it must resolve to the same value.
        self.assertEqual(intervals[2].start_ns, 1000000)
        self.assertEqual(intervals[2].duration_ns, 2000000)


# --- aggregate ---------------------------------------------------------------------------------

class AggregateTests(unittest.TestCase):
    def test_aggregate_math_for_the_two_sample_label(self):
        intervals = _sample_intervals()

        agg = xctracelib.aggregate(intervals)

        shadow = agg[_SHADOW]
        self.assertEqual(shadow["count"], 2)
        self.assertEqual(shadow["totalMs"], 4.0)
        self.assertEqual(shadow["meanMs"], 2.0)
        # Nearest-rank on the sorted durations [1, 3]: p50 -> rank ceil(0.5*2)=1 -> 1ms;
        # p95 -> rank ceil(0.95*2)=2 -> 3ms.
        self.assertEqual(shadow["p50Ms"], 1.0)
        self.assertEqual(shadow["p95Ms"], 3.0)
        self.assertEqual(shadow["maxMs"], 3.0)

    def test_aggregate_for_the_single_sample_label(self):
        intervals = _sample_intervals()

        agg = xctracelib.aggregate(intervals)

        scene = agg[_SCENE]
        self.assertEqual(scene["count"], 1)
        self.assertEqual(scene["totalMs"], 2.0)
        self.assertEqual(scene["meanMs"], 2.0)
        self.assertEqual(scene["p50Ms"], 2.0)
        self.assertEqual(scene["p95Ms"], 2.0)
        self.assertEqual(scene["maxMs"], 2.0)

    def test_frame_marker_label_adds_an_approximate_frame_total(self):
        intervals = _sample_intervals()

        agg = xctracelib.aggregate(intervals, frame_marker_label=_SCENE)

        # scenePass fires once -> treated as "1 frame" -> frame total == sum of every label's
        # totalMs (4.0 + 2.0) / 1.
        self.assertIn(xctracelib.FRAME_TOTAL_KEY, agg)
        self.assertEqual(agg[xctracelib.FRAME_TOTAL_KEY], 6.0)

    def test_frame_marker_label_absent_from_intervals_adds_nothing(self):
        intervals = _sample_intervals()

        agg = xctracelib.aggregate(intervals, frame_marker_label="lmx.does.not.appear")

        self.assertNotIn(xctracelib.FRAME_TOTAL_KEY, agg)


# --- check_anomalies -----------------------------------------------------------------------

class CheckAnomaliesTests(unittest.TestCase):
    def test_no_encoders_matched_fires_when_zero_labels_contain_lmx(self):
        tree = xctracelib.resolve_refs(_load("xctrace-no-lmx.xml"))
        intervals = xctracelib.encoder_intervals(tree)
        agg = xctracelib.aggregate(intervals)

        anomalies = xctracelib.check_anomalies(agg)

        self.assertEqual(len(anomalies), 1)
        self.assertEqual(anomalies[0]["check"], "no-encoders-matched")
        self.assertEqual(anomalies[0]["severity"], "error")

    def test_no_encoders_matched_does_not_fire_when_lmx_labels_are_present(self):
        agg = xctracelib.aggregate(_sample_intervals())

        anomalies = xctracelib.check_anomalies(agg)

        self.assertNotIn("no-encoders-matched", [a["check"] for a in anomalies])

    def test_unstable_pass_fires_at_or_above_the_eight_sample_floor(self):
        agg = {"lmx.render.jitter": {"count": 8, "totalMs": 16.0, "meanMs": 2.0,
                                     "p50Ms": 1.0, "p95Ms": 3.0, "maxMs": 3.0}}

        anomalies = xctracelib.check_anomalies(agg)

        self.assertEqual(len(anomalies), 1)
        self.assertEqual(anomalies[0]["check"], "unstable-pass")
        self.assertEqual(anomalies[0]["severity"], "warning")
        self.assertEqual(anomalies[0]["subject"], "lmx.render.jitter")
        # One label can cover more than one pass, so the hint must not claim it is about one pass.
        hint = anomalies[0]["next_steps_hint"]
        self.assertIn("this encoder label", hint)
        self.assertNotIn("this pass", hint)

    def test_unstable_pass_does_not_fire_below_the_eight_sample_floor(self):
        # Same p95 > 2x p50 ratio as the firing case above, but only 7 samples -- per the rule
        # (xctracelib.MIN_SAMPLES_FOR_UNSTABLE), that's noise, not a finding.
        agg = {"lmx.render.jitter": {"count": 7, "totalMs": 14.0, "meanMs": 2.0,
                                     "p50Ms": 1.0, "p95Ms": 3.0, "maxMs": 3.0}}

        anomalies = xctracelib.check_anomalies(agg)

        self.assertEqual(anomalies, [])

    def test_unstable_pass_does_not_fire_when_p95_is_within_2x_p50(self):
        agg = {"lmx.render.steady": {"count": 20, "totalMs": 40.0, "meanMs": 2.0,
                                     "p50Ms": 2.0, "p95Ms": 3.5, "maxMs": 3.6}}

        anomalies = xctracelib.check_anomalies(agg)

        self.assertEqual(anomalies, [])

    def test_anomaly_shape_matches_anomalylib(self):
        # Matches anomalylib._anomaly's shape for consistency across the two tools' outputs.
        agg = {}
        anomalies = xctracelib.check_anomalies(agg)
        self.assertEqual(len(anomalies), 1)
        self.assertEqual(set(anomalies[0].keys()),
                         {"check", "severity", "subject", "finding", "next_steps_hint"})


if __name__ == "__main__":
    unittest.main()
