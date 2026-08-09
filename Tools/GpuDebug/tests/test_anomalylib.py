"""Tests for anomalylib.py -- the checks that turn a dump's manifest into a diagnosis.

Every check gets a firing case and a non-firing case, driven by hand-built manifest/uniform dicts
rather than by a real bundle: the point of these tests is the *classification rule*, and a rule is
only worth trusting if the shape that must NOT fire is asserted as carefully as the shape that
must. Two rules get extra attention because getting them wrong makes the tool actively
misleading -- the `lmx.render.sceneDepth` capture artifact (a known all-NaN plane that is not
evidence about the frame, and must never read as an error) and the transpose in the
shadow-transform cross-check (uploads are decoded row-major, `shadowmath` computes column-major;
a missing transpose would fire on every healthy capture).

Run: python3 -m unittest discover -s Tools/GpuDebug/tests -v
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

import anomalylib  # noqa: E402
import schemalib  # noqa: E402
import shadowmath  # noqa: E402

_SHADOW_MAP = "lmx.render.shadowMap"
_SCENE_COLOR = "lmx.render.sceneColor"
_SCENE_DEPTH = "lmx.render.sceneDepth"

_SPHERE = [0.0, 0.0, 0.0, 42.43]
_LIGHT_DIR = [0.577, -0.577, 0.577]


# --- Fixture builders --------------------------------------------------------------------------

def _texture(label, kind="texture2d", width=8, height=8):
    return schemalib.Resource(label=label, kind=kind, format="BGRA8Unorm", width=width,
                              height=height, mip_levels=1, size_bytes=None)


def _schema(resources=None, uniform_uploads=None, context=None):
    return schemalib.Schema(
        context=context if context is not None else _context(),
        resources=resources if resources is not None else [_texture(_SHADOW_MAP),
                                                           _texture(_SCENE_COLOR)],
        uniform_structs=[],
        uniform_uploads=uniform_uploads if uniform_uploads is not None else [
            schemalib.UniformUpload(ring_label="lmx.device.uniformRing.0", slot=2, ring_offset=0,
                                    size_bytes=288)],
    )


def _context(bounding_sphere=None, light_dir=None):
    return {
        "sceneName": "LumineDefault",
        "frameIndex": 3,
        "cameraPos": [0.0, 0.0, 0.0],
        "boundingSphere": _SPHERE if bounding_sphere is None else bounding_sphere,
        "light0Direction": _LIGHT_DIR if light_dir is None else light_dir,
        "light0Strength": [1.0, 1.0, 1.0],
        "ambient": [0.1, 0.1, 0.1],
        "shadowFilter": "PCF",
    }


def _resources(matched=None, matched_undecodable=None, unmatched_blobs=None, missing=None):
    return {
        "matched": [{"resource": {"label": label}, "blob": {"path": f"{label}.blob",
                                                            "sizeBytes": 1024}}
                    for label in (matched if matched is not None else [_SHADOW_MAP, _SCENE_COLOR])],
        "matchedUndecodable": [{"resource": {"label": label}, "reason": reason}
                               for label, reason in (matched_undecodable or [])],
        "unmatchedBlobs": [{"path": name, "sizeBytes": size}
                           for name, size in (unmatched_blobs or [])],
        "missingResources": [{"label": label} for label in (missing or [])],
    }


def _depth_stats(min_v=0.0, max_v=1.0, mean=0.5, fraction_at_clear=0.4, flat=False):
    return {"min": min_v, "max": max_v, "mean": mean, "fractionAtClear": fraction_at_clear,
            "flat": flat}


def _color_stats(flat=False):
    return {"r": {"min": 0, "max": 255, "mean": 100.0},
            "g": {"min": 0, "max": 255, "mean": 100.0},
            "b": {"min": 0, "max": 255, "mean": 100.0},
            "flat": flat}


def _image(label, stats, fmt="BGRA8Unorm"):
    return {"label": label, "file": "x.png", "format": fmt, "width": 8, "height": 8,
            "stats": stats}


def _row_major(column_major):
    """glm/shadowmath storage (`m[col][row]`) -> the row-major shape uniformlib emits for display
    (`rows[row][col]`). Exactly the transpose the cross-check has to undo."""
    return [[column_major[c][r] for c in range(4)] for r in range(4)]


def _healthy_shadow_transform():
    return _row_major(shadowmath.fit_shadow_ortho(_SPHERE, _LIGHT_DIR)[1])


def _upload(index=0, struct_name="PassUniforms", values=None, finite=True, slot=2):
    return {
        "index": index,
        "structName": struct_name,
        "slot": slot,
        "ringLabel": "lmx.device.uniformRing.0",
        "ringOffset": index * 288,
        "values": values if values is not None else {
            "viewProj": _row_major(shadowmath.identity()),
            "shadowTransform": _healthy_shadow_transform(),
            "eyePos": [1.0, 2.0, 3.0],
            "shadowFilter": 0,
        },
        "finite": finite,
    }


def _uniforms(uploads=None, attribution="single ring-sized blob, attributed to ring 0"):
    return {"version": 1, "matrixOrder": "row-major (transposed from column-major storage)",
            "ringAttribution": attribution,
            "uploads": [_upload()] if uploads is None else uploads}


def _manifest(images=None, resources=None, context=None):
    return {
        "version": 1,
        "capture": context if context is not None else _context(),
        "resources": resources if resources is not None else _resources(),
        "images": images if images is not None else [
            _image(_SHADOW_MAP, _depth_stats(), fmt="Depth32Float"),
            _image(_SCENE_COLOR, _color_stats()),
        ],
        "anomalies": [],
    }


def _run(manifest=None, uniforms=None, schema=None):
    return anomalylib.run_checks(manifest if manifest is not None else _manifest(),
                                 uniforms if uniforms is not None else _uniforms(),
                                 schema if schema is not None else _schema())


def _checks(anomalies, name):
    return [a for a in anomalies if a["check"] == name]


# --- The healthy baseline ----------------------------------------------------------------------

class HealthyCaptureTests(unittest.TestCase):
    def test_a_healthy_capture_produces_no_errors_or_warnings(self):
        anomalies = _run()
        self.assertEqual([a for a in anomalies if a["severity"] != "info"], [],
                         msg=f"unexpected non-info anomalies: {anomalies}")

    def test_every_anomaly_carries_the_four_documented_keys(self):
        manifest = _manifest(images=[_image(_SHADOW_MAP, _depth_stats(fraction_at_clear=1.0),
                                            fmt="Depth32Float")],
                             resources=_resources(unmatched_blobs=[("MTLBuffer-92-0", 64)]))
        for anomaly in _run(manifest=manifest):
            self.assertEqual(set(anomaly), {"check", "severity", "subject", "finding",
                                            "next_steps_hint"})
            self.assertIn(anomaly["severity"], ("error", "warning", "info"))

    def test_output_is_severity_ordered_errors_first(self):
        manifest = _manifest(
            images=[_image(_SHADOW_MAP, _depth_stats(fraction_at_clear=1.0), fmt="Depth32Float"),
                    _image(_SCENE_COLOR, _color_stats(flat=True))],
            resources=_resources(unmatched_blobs=[("MTLBuffer-92-0", 64)]))
        ranks = [{"error": 0, "warning": 1, "info": 2}[a["severity"]] for a in _run(manifest)]
        self.assertEqual(ranks, sorted(ranks))

    def test_run_checks_is_deterministic(self):
        manifest = _manifest(
            images=[_image("lmx.render.zzz", _color_stats(flat=True)),
                    _image("lmx.render.aaa", _color_stats(flat=True))],
            resources=_resources(unmatched_blobs=[("MTLBuffer-92-0", 64)]))
        self.assertEqual(_run(manifest), _run(manifest))

    def test_flat_image_subjects_sort_by_label(self):
        manifest = _manifest(images=[_image("lmx.render.zzz", _color_stats(flat=True)),
                                     _image("lmx.render.aaa", _color_stats(flat=True))])
        subjects = [a["subject"] for a in _checks(_run(manifest), "flat-image")]
        self.assertEqual(subjects, ["lmx.render.aaa", "lmx.render.zzz"])


# --- Check 1: flat-image -----------------------------------------------------------------------

class FlatImageTests(unittest.TestCase):
    def test_fires_warning_for_a_flat_color_image(self):
        manifest = _manifest(images=[_image(_SCENE_COLOR, _color_stats(flat=True))])
        found = _checks(_run(manifest), "flat-image")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]["severity"], "warning")
        self.assertEqual(found[0]["subject"], _SCENE_COLOR)
        self.assertIn("nothing rendered into", found[0]["next_steps_hint"])

    def test_fires_warning_for_a_depth_image_entirely_at_clear(self):
        manifest = _manifest(images=[_image("lmx.render.otherDepth",
                                            _depth_stats(fraction_at_clear=1.0),
                                            fmt="Depth32Float")])
        found = _checks(_run(manifest), "flat-image")
        self.assertEqual([a["severity"] for a in found], ["warning"])

    def test_does_not_fire_for_an_image_with_content(self):
        self.assertEqual(_checks(_run(), "flat-image"), [])

    def test_scene_depth_all_nan_plane_is_info_not_warning(self):
        """The known capture artifact: lmx.render.sceneDepth is renderTarget-only and never
        sampled, so Xcode cannot resolve its contents and dumps an all-NaN plane (null stats, flat
        true). That is a fact about the capture, not about the frame -- reporting it as a warning
        would send a reader hunting a bug that isn't there."""
        manifest = _manifest(images=[_image(_SCENE_DEPTH,
                                            _depth_stats(min_v=None, max_v=None, mean=None,
                                                         fraction_at_clear=0.0, flat=True),
                                            fmt="Depth32Float")])
        found = _checks(_run(manifest), "flat-image")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]["severity"], "info")
        self.assertEqual(found[0]["subject"], _SCENE_DEPTH)
        self.assertIn("never sampled", found[0]["finding"])

    def test_shadow_map_gets_no_flat_warning_when_the_empty_check_owns_it(self):
        """One fact, one anomaly: an all-clear shadow map is check 2's error, and check 1 must not
        also emit a vaguer warning about the same texels."""
        manifest = _manifest(images=[_image(_SHADOW_MAP, _depth_stats(fraction_at_clear=1.0,
                                                                      flat=True),
                                            fmt="Depth32Float")])
        anomalies = _run(manifest)
        self.assertEqual(_checks(anomalies, "flat-image"), [])
        self.assertEqual(len(_checks(anomalies, "empty-shadow-map")), 1)


# --- Check 2: empty-shadow-map -----------------------------------------------------------------

class EmptyShadowMapTests(unittest.TestCase):
    def test_fires_error_when_the_shadow_map_is_at_clear(self):
        manifest = _manifest(images=[_image(_SHADOW_MAP, _depth_stats(fraction_at_clear=0.995),
                                            fmt="Depth32Float")])
        found = _checks(_run(manifest), "empty-shadow-map")
        self.assertEqual([a["severity"] for a in found], ["error"])
        self.assertIn("shadow pass", found[0]["next_steps_hint"])
        self.assertIn("fitShadowOrtho", found[0]["next_steps_hint"])

    def test_fires_error_when_the_shadow_map_is_an_all_nan_plane(self):
        """The shadow map is sampled (texture slot t3), so it does NOT get sceneDepth's
        capture-artifact excuse -- an all-NaN shadow map is a real finding."""
        manifest = _manifest(images=[_image(_SHADOW_MAP,
                                            _depth_stats(min_v=None, max_v=None, mean=None,
                                                         fraction_at_clear=0.0, flat=True),
                                            fmt="Depth32Float")])
        found = _checks(_run(manifest), "empty-shadow-map")
        self.assertEqual([a["severity"] for a in found], ["error"])
        self.assertIn("non-finite", found[0]["finding"])

    def test_does_not_fire_when_the_shadow_map_has_occluders(self):
        self.assertEqual(_checks(_run(), "empty-shadow-map"), [])

    def test_does_not_fire_for_a_partly_covered_shadow_map(self):
        manifest = _manifest(images=[_image(_SHADOW_MAP, _depth_stats(fraction_at_clear=0.98),
                                            fmt="Depth32Float")])
        self.assertEqual(_checks(_run(manifest), "empty-shadow-map"), [])


# --- Check 3: nan-uniform ----------------------------------------------------------------------

class NanUniformTests(unittest.TestCase):
    def test_fires_error_naming_struct_and_field(self):
        values = {"viewProj": _row_major(shadowmath.identity()),
                  "shadowTransform": _healthy_shadow_transform(),
                  "eyePos": [1.0, None, 3.0]}
        found = _checks(_run(uniforms=_uniforms([_upload(values=values, finite=False)])),
                        "nan-uniform")
        self.assertEqual([a["severity"] for a in found], ["error"])
        self.assertEqual(found[0]["subject"], "PassUniforms.eyePos")

    def test_fires_once_per_field_even_across_repeated_uploads(self):
        values = {"eyePos": [None, None, None], "shadowTransform": _healthy_shadow_transform()}
        uploads = [_upload(index=0, values=values, finite=False),
                   _upload(index=1, values=values, finite=False)]
        found = _checks(_run(uniforms=_uniforms(uploads)), "nan-uniform")
        self.assertEqual([a["subject"] for a in found], ["PassUniforms.eyePos"])
        self.assertIn("0, 1", found[0]["finding"])

    def test_fires_for_a_non_finite_matrix_entry(self):
        matrix = _row_major(shadowmath.identity())
        matrix[2][3] = None
        found = _checks(_run(uniforms=_uniforms([_upload(values={"viewProj": matrix},
                                                          finite=False)])), "nan-uniform")
        self.assertEqual([a["subject"] for a in found], ["PassUniforms.viewProj"])

    def test_does_not_fire_for_finite_uploads(self):
        self.assertEqual(_checks(_run(), "nan-uniform"), [])

    def test_does_not_mistake_an_integer_field_for_a_missing_float(self):
        values = {"shadowTransform": _healthy_shadow_transform(), "shadowFilter": 0, "flags": 0}
        self.assertEqual(_checks(_run(uniforms=_uniforms([_upload(values=values)])),
                                 "nan-uniform"), [])


# --- Check 4: degenerate-matrix ----------------------------------------------------------------

class DegenerateMatrixTests(unittest.TestCase):
    def test_fires_error_for_an_all_zero_view_proj(self):
        values = {"viewProj": [[0.0] * 4 for _ in range(4)],
                  "shadowTransform": _healthy_shadow_transform()}
        found = _checks(_run(uniforms=_uniforms([_upload(values=values)])), "degenerate-matrix")
        self.assertEqual([a["severity"] for a in found], ["error"])
        self.assertEqual(found[0]["subject"], "PassUniforms.viewProj")

    def test_fires_error_for_a_collapsed_shadow_transform(self):
        collapsed = _row_major(shadowmath.identity())
        collapsed[2][2] = 0.0  # an axis scaled to nothing -> determinant 0
        found = _checks(_run(uniforms=_uniforms([_upload(values={"shadowTransform": collapsed})])),
                        "degenerate-matrix")
        self.assertEqual([a["subject"] for a in found], ["PassUniforms.shadowTransform"])

    def test_does_not_fire_for_the_real_shadow_transform(self):
        """The genuine bake has a determinant of 0.5 * -0.5 * (z scale) -- small, but nowhere near
        the 1e-12 threshold. A check that used a naive "close to zero" bar would flag every healthy
        capture, so this is the case that pins the threshold."""
        self.assertEqual(_checks(_run(), "degenerate-matrix"), [])

    def test_skips_a_matrix_with_non_finite_entries(self):
        """Check 3 already owns a NaN matrix; a determinant over Nones would just crash."""
        matrix = _row_major(shadowmath.identity())
        matrix[0][0] = None
        anomalies = _run(uniforms=_uniforms([_upload(values={"viewProj": matrix}, finite=False)]))
        self.assertEqual(_checks(anomalies, "degenerate-matrix"), [])
        self.assertEqual(len(_checks(anomalies, "nan-uniform")), 1)


# --- Check 5: missing-expected-resource --------------------------------------------------------

class MissingExpectedResourceTests(unittest.TestCase):
    def test_fires_error_when_the_shadow_map_is_absent_from_the_schema(self):
        found = _checks(_run(schema=_schema(resources=[_texture(_SCENE_COLOR)])),
                        "missing-expected-resource")
        self.assertEqual([a["severity"] for a in found], ["error"])
        self.assertEqual(found[0]["subject"], _SHADOW_MAP)
        self.assertIn("never created", found[0]["next_steps_hint"])

    def test_fires_error_when_a_resource_is_in_the_schema_but_not_in_the_bundle(self):
        manifest = _manifest(resources=_resources(matched=[_SHADOW_MAP], missing=[_SCENE_COLOR]))
        found = _checks(_run(manifest), "missing-expected-resource")
        self.assertEqual([a["subject"] for a in found], [_SCENE_COLOR])
        self.assertIn("not in the bundle", found[0]["next_steps_hint"])

    def test_fires_error_when_the_shadow_map_has_no_captured_contents(self):
        """A fallback texture with no blob is normal, but the shadow map
        is dumped in every healthy capture, so its absence is a finding."""
        manifest = _manifest(resources=_resources(
            matched=[_SCENE_COLOR],
            matched_undecodable=[(_SHADOW_MAP, "no contents captured")]))
        found = _checks(_run(manifest), "missing-expected-resource")
        self.assertEqual([a["subject"] for a in found], [_SHADOW_MAP])

    def test_does_not_fire_when_both_expected_resources_are_matched(self):
        self.assertEqual(_checks(_run(), "missing-expected-resource"), [])

    def test_does_not_fire_for_an_unrelated_texture_with_no_contents(self):
        manifest = _manifest(resources=_resources(
            matched_undecodable=[("lmx.render.whiteFallback", "no contents captured")]))
        self.assertEqual(_checks(_run(manifest), "missing-expected-resource"), [])


# --- Check 6: no-pass-uniforms-upload ----------------------------------------------------------

class NoPassUniformsUploadTests(unittest.TestCase):
    def test_fires_error_when_only_sky_uniforms_were_uploaded(self):
        uploads = [_upload(struct_name="SkyUniforms",
                           values={"viewProj": _row_major(shadowmath.identity()),
                                   "eyePos": [0.0, 0.0, 0.0]})]
        found = _checks(_run(uniforms=_uniforms(uploads)), "no-pass-uniforms-upload")
        self.assertEqual([a["severity"] for a in found], ["error"])
        self.assertIn("mid-scene-switch", found[0]["next_steps_hint"])

    def test_fires_error_when_the_capture_recorded_no_uploads_at_all(self):
        found = _checks(_run(uniforms=_uniforms(uploads=[],
                                                 attribution="no uniform uploads recorded in "
                                                             "this capture"),
                             schema=_schema(uniform_uploads=[])), "no-pass-uniforms-upload")
        self.assertEqual([a["severity"] for a in found], ["error"])

    def test_does_not_fire_when_pass_uniforms_were_uploaded(self):
        self.assertEqual(_checks(_run(), "no-pass-uniforms-upload"), [])


# --- Uniform decode unavailable: the degrade-to-info path for checks 3, 4, 6, 7 ----------------

class UniformDecodeUnavailableTests(unittest.TestCase):
    def _declined(self):
        """The shape uniformlib produces when its attribution policy declines: the schema recorded
        uploads, but no ring blob could be attributed, so nothing was decoded."""
        return _uniforms(uploads=[], attribution="found 0 MTLBuffer-* blob(s) sized 262144 bytes "
                                                  "(expected 1, or 3 to match sibling ring "
                                                  "resources); ring bytes not attributed")

    def test_emits_one_info_naming_the_attribution_note(self):
        found = _checks(_run(uniforms=self._declined()), "uniform-decode-unavailable")
        self.assertEqual([a["severity"] for a in found], ["info"])
        self.assertIn("uniform decode unavailable:", found[0]["finding"])
        self.assertIn("ring bytes not attributed", found[0]["finding"])

    def test_suppresses_the_uniform_dependent_checks_rather_than_firing_them(self):
        anomalies = _run(uniforms=self._declined())
        for check in ("nan-uniform", "degenerate-matrix", "no-pass-uniforms-upload",
                      "shadow-transform-mismatch"):
            self.assertEqual(_checks(anomalies, check), [], msg=f"{check} fired without uniforms")
        self.assertEqual([a for a in anomalies if a["severity"] == "error"], [])

    def test_a_capture_that_simply_had_no_uploads_is_not_treated_as_unavailable(self):
        """"the engine recorded zero uploads" and "the decoder could not read the ring" are
        different findings: the first is check 6's error, the second is not the frame's fault."""
        anomalies = _run(uniforms=_uniforms(uploads=[]), schema=_schema(uniform_uploads=[]))
        self.assertEqual(_checks(anomalies, "uniform-decode-unavailable"), [])
        self.assertEqual(len(_checks(anomalies, "no-pass-uniforms-upload")), 1)


# --- Check 7: shadow-transform-mismatch --------------------------------------------------------

class ShadowTransformMismatchTests(unittest.TestCase):
    def test_does_not_fire_when_the_upload_matches_the_recompute(self):
        """The transpose regression test. Uploads are decoded row-major; shadowmath computes
        column-major. If the cross-check forgot to transpose one side, this healthy fixture would
        fire -- which is why the fixture builds its upload by transposing the recompute rather than
        by copying whatever the checker produces."""
        self.assertEqual(_checks(_run(), "shadow-transform-mismatch"), [])

    def test_fires_error_when_the_uploaded_transform_drifts(self):
        drifted = _healthy_shadow_transform()
        drifted[0][3] += 0.5
        found = _checks(_run(uniforms=_uniforms([_upload(values={
            "shadowTransform": drifted})])), "shadow-transform-mismatch")
        self.assertEqual([a["severity"] for a in found], ["error"])
        self.assertIn("stale upload", found[0]["next_steps_hint"])

    def test_compares_against_the_last_pass_uniforms_upload(self):
        """Multiple PassUniforms uploads in one frame are possible; the last one is what the scene
        pass actually drew with. Matching on structName rather than slot matters here too -- the
        sky's SkyUniforms shares slot 2."""
        stale = _healthy_shadow_transform()
        stale[0][3] += 5.0
        uploads = [_upload(index=0, values={"shadowTransform": stale}),
                   _upload(index=1, struct_name="SkyUniforms",
                           values={"viewProj": _row_major(shadowmath.identity())}),
                   _upload(index=2, values={"shadowTransform": _healthy_shadow_transform()})]
        self.assertEqual(_checks(_run(uniforms=_uniforms(uploads)), "shadow-transform-mismatch"),
                         [])

    def test_tolerates_float32_rounding(self):
        """The engine computes in float32 and this recompute runs in doubles, so exact equality is
        the wrong bar -- a relative 1e-3 is loose enough for the precision gap and far tighter than
        any real drift."""
        rounded = [[float(f"{value:.6g}") for value in row] for row in _healthy_shadow_transform()]
        self.assertEqual(_checks(_run(uniforms=_uniforms([_upload(values={
            "shadowTransform": rounded})])), "shadow-transform-mismatch"), [])

    def test_degrades_to_info_when_the_context_has_null_floats(self):
        """A non-finite float in the sidecar's scene state comes back as JSON null. There is
        nothing to recompute from, and saying so beats guessing."""
        context = _context(bounding_sphere=[0.0, 0.0, 0.0, None])
        found = _checks(_run(manifest=_manifest(context=context),
                             schema=_schema(context=context)), "shadow-transform-mismatch")
        self.assertEqual([a["severity"] for a in found], ["info"])
        self.assertIn("cannot recompute", found[0]["finding"])

    def test_degrades_to_info_when_the_bounding_sphere_is_degenerate(self):
        context = _context(bounding_sphere=[0.0, 0.0, 0.0, 0.0])
        found = _checks(_run(manifest=_manifest(context=context),
                             schema=_schema(context=context)), "shadow-transform-mismatch")
        self.assertEqual([a["severity"] for a in found], ["info"])

    def test_degrades_to_info_when_the_light_direction_is_the_zero_vector(self):
        context = _context(light_dir=[0.0, 0.0, 0.0])
        found = _checks(_run(manifest=_manifest(context=context),
                             schema=_schema(context=context)), "shadow-transform-mismatch")
        self.assertEqual([a["severity"] for a in found], ["info"])

    def test_skips_silently_when_the_upload_transform_is_non_finite(self):
        """Check 3 owns a NaN upload; comparing Nones against floats here would only add noise."""
        matrix = _healthy_shadow_transform()
        matrix[1][1] = None
        anomalies = _run(uniforms=_uniforms([_upload(values={"shadowTransform": matrix},
                                                      finite=False)]))
        self.assertEqual(_checks(anomalies, "shadow-transform-mismatch"), [])


# --- Check 8: unmatched-blobs ------------------------------------------------------------------

class UnmatchedBlobsTests(unittest.TestCase):
    def test_reports_count_and_total_bytes(self):
        manifest = _manifest(resources=_resources(unmatched_blobs=[("MTLBuffer-92-0", 100),
                                                                    ("CAMetalLayer-2-index-3",
                                                                     250)]))
        found = _checks(_run(manifest), "unmatched-blobs")
        self.assertEqual([a["severity"] for a in found], ["info"])
        self.assertIn("2", found[0]["finding"])
        self.assertIn("350", found[0]["finding"])

    def test_does_not_fire_when_every_blob_was_reached(self):
        self.assertEqual(_checks(_run(), "unmatched-blobs"), [])


# --- Check 9: undecodable-resource -------------------------------------------------------------

class UndecodableResourceTests(unittest.TestCase):
    def test_fires_warning_for_a_header_schema_geometry_disagreement(self):
        manifest = _manifest(resources=_resources(
            matched_undecodable=[("lmx.test.tex", "header geometry disagrees with schema")]))
        found = _checks(_run(manifest), "undecodable-resource")
        self.assertEqual([a["severity"] for a in found], ["warning"])
        self.assertEqual(found[0]["subject"], "lmx.test.tex")

    def test_fires_warning_when_the_header_payload_overruns_the_blob(self):
        manifest = _manifest(resources=_resources(
            matched_undecodable=[("lmx.test.tex",
                                  "header bytesPerImage exceeds the blob file size")]))
        self.assertEqual([a["severity"] for a in _checks(_run(manifest), "undecodable-resource")],
                         ["warning"])

    def test_does_not_fire_for_a_bc1_texture(self):
        """BC1 is the most common format in a real capture (all scene albedo plus the sky
        cubemap); "no pixel decode" there is the documented normal path, not a finding."""
        manifest = _manifest(resources=_resources(matched_undecodable=[
            ("LumineDefault.tile",
             "LumineDefault.tile: BC1_RGBA_sRGB (compressed) is out of scope for pixel decode -- "
             "this is the normal case for scene albedo and the sky cubemap, not a broken manifest")
        ]))
        self.assertEqual(_checks(_run(manifest), "undecodable-resource"), [])

    def test_does_not_fire_for_a_fallback_texture_with_no_contents(self):
        manifest = _manifest(resources=_resources(
            matched_undecodable=[("lmx.render.whiteFallback", "no contents captured")]))
        self.assertEqual(_checks(_run(manifest), "undecodable-resource"), [])


if __name__ == "__main__":
    unittest.main()
