"""Turn a capture dump into a diagnosis of the frame and likely inspection points.

Every check emits zero or more anomalies shaped
`{"check", "severity", "subject", "finding", "next_steps_hint"}` -- `finding` states what was
observed, `next_steps_hint` states where to look, and they are kept separate so a reader can trust
the first without having to agree with the second. `check` is the stable machine-readable identity
used by tests and downstream filters.

Two classification rules carry most of this module's value, and both are about *not* crying wolf:

- **`lmx.render.sceneDepth` is a known capture artifact, not evidence.** It is renderTarget-only
  and never sampled, so Xcode's GPU capture cannot resolve its contents and dumps an all-NaN plane
  (null stats, `flat` true). Reporting that as "nothing rendered into your depth buffer" would send
  a reader hunting a bug that does not exist, so it degrades to an info note that names the
  artifact. `lmx.render.shadowMap` gets no such excuse -- it *is* sampled (texture slot t3), so an
  all-NaN or all-clear shadow map there is a real error.
- **A missing blob is usually normal.** Real App captures may omit blobs for labelled textures
  and most sky-cubemap mips. Only
  `lmx.render.shadowMap` and `lmx.render.sceneColor` are dumped in every healthy capture, so only
  those two are checked for presence.

Determinism is a requirement, not an accident: checks run in a fixed order, subjects sort by label
within a check, and the final severity sort is stable -- so two dumps of the same capture produce
byte-identical `anomalies` arrays and a diff between two dumps means something changed.

Run: python3 -c "import anomalylib"  (see tests/test_anomalylib.py for worked examples)
"""
import shadowmath

SHADOW_MAP_LABEL = "lmx.render.shadowMap"
SCENE_COLOR_LABEL = "lmx.render.sceneColor"
SCENE_DEPTH_LABEL = "lmx.render.sceneDepth"

# The two resources every healthy App capture dumps. Everything else absent is normal.
EXPECTED_LABELS = (SCENE_COLOR_LABEL, SHADOW_MAP_LABEL)

# A depth image at least this fraction at the clear value counts as "the pass wrote nothing".
# Not 1.0: a few texels of a 2048x2048 map differing is still, for diagnostic purposes, empty.
_EMPTY_SHADOW_FRACTION = 0.99

# |det| below this is a collapsed matrix. The real shadowTransform's determinant is ~0.25 times
# the projection's z scale -- small, but many orders of magnitude above this bar, which is what
# keeps a healthy capture quiet (tests/test_anomalylib.py pins that).
_DEGENERATE_DET = 1e-12

# Relative tolerance for the shadow-transform cross-check: the engine computes in float32 and
# shadowmath in Python doubles, so exact equality is the wrong bar. Scaled by max(1, |expected|)
# so a near-zero expected entry is judged absolutely and a large one proportionally.
_MATRIX_REL_TOLERANCE = 1e-3

# Fields whose value is a 4x4 matrix worth checking for collapse (check 4).
_MATRIX_FIELDS = ("viewProj", "shadowTransform")

# bundlelib.join()'s reason for "labelled, but no blob file on disk". Normal for fallback textures
# and higher sky mips; notable only for the two EXPECTED_LABELS.
_NO_CONTENTS = "no contents captured"

# decodelib's BC1 refusal. The single most common format in a real capture, and documented as the
# normal path -- never an anomaly.
_BC1_MARKER = "out of scope for pixel decode"

_SEVERITY_RANK = {"error": 0, "warning": 1, "info": 2}


def _anomaly(check: str, severity: str, subject: str, finding: str, hint: str) -> dict:
    return {"check": check, "severity": severity, "subject": subject, "finding": finding,
            "next_steps_hint": hint}


# --- Small readers over the manifest's JSON shapes ---------------------------------------------

def _stats(image: dict) -> dict:
    return image.get("stats") or {}


def _is_all_nan_plane(stats: dict) -> bool:
    """A depth plane whose every texel is the same non-finite bit pattern: decodelib reports
    `flat` true (bit-pattern equality) but min/max/mean null, because it only ever summarizes
    finite samples."""
    return stats.get("flat") is True and stats.get("min") is None


def _looks_empty(stats: dict) -> bool:
    return stats.get("flat") is True or stats.get("fractionAtClear") == 1.0


def _shadow_map_is_empty(stats: dict) -> bool:
    """Check 2's predicate, factored out because check 1 has to defer to it: one fact should
    produce one anomaly, so the generic flat-image warning stays quiet when this specific error
    is going to fire about the same texels."""
    if _is_all_nan_plane(stats):
        return True
    fraction = stats.get("fractionAtClear")
    return fraction is not None and fraction >= _EMPTY_SHADOW_FRACTION


def _has_none(value) -> bool:
    """True if `value` -- a decoded uniform field, so a float, an int, a string, or nested lists
    of those -- contains a JSON null anywhere. uniformlib renders every non-finite float as null,
    so this is how a NaN is spotted after the fact."""
    if value is None:
        return True
    if isinstance(value, list):
        return any(_has_none(item) for item in value)
    return False


def _undecodable_reasons(resources: dict) -> dict:
    """label -> reason, for every entry in the manifest's `matchedUndecodable` bucket."""
    return {entry["resource"]["label"]: entry["reason"]
            for entry in resources.get("matchedUndecodable", [])}


# --- Check 1: flat-image -----------------------------------------------------------------------

def _check_flat_image(images: list) -> list:
    out = []
    for image in sorted(images, key=lambda i: i.get("label", "")):
        label, stats = image.get("label", ""), _stats(image)
        if not _looks_empty(stats):
            continue
        if label == SHADOW_MAP_LABEL and _shadow_map_is_empty(stats):
            continue  # check 2 owns this one, with a sharper finding
        if label == SCENE_DEPTH_LABEL:
            detail = ("every texel is non-finite (NaN)" if _is_all_nan_plane(stats)
                      else "every texel holds the same value")
            out.append(_anomaly(
                "flat-image", "info", label,
                f"{label} came back as a uniform plane ({detail}), which is the known capture "
                "artifact rather than a fact about the frame: this texture is renderTarget-only "
                "and never sampled, so a GPU capture cannot resolve its contents",
                "nothing to do -- read the scene colour image instead; if you need real depth "
                "values, give the texture ShaderRead usage before capturing"))
            continue
        out.append(_anomaly(
            "flat-image", "warning", label,
            f"every texel of {label} holds the same value"
            + (" (all at the clear depth 1.0)" if stats.get("fractionAtClear") == 1.0 else ""),
            f"nothing rendered into {label} this frame; check the pass that writes it ran and "
            "its draws weren't culled"))
    return out


# --- Check 2: empty-shadow-map -----------------------------------------------------------------

def _check_empty_shadow_map(images: list) -> list:
    out = []
    for image in sorted(images, key=lambda i: i.get("label", "")):
        if image.get("label") != SHADOW_MAP_LABEL:
            continue
        stats = _stats(image)
        if not _shadow_map_is_empty(stats):
            continue
        if _is_all_nan_plane(stats):
            finding = (f"{SHADOW_MAP_LABEL} came back with every texel non-finite (NaN). Unlike "
                       f"{SCENE_DEPTH_LABEL}, this texture IS sampled (texture slot t3), so the "
                       "capture had contents to resolve -- an all-NaN plane means nothing valid "
                       "was ever written into it")
        else:
            fraction = stats.get("fractionAtClear")
            finding = (f"{fraction:.4f} of {SHADOW_MAP_LABEL}'s texels sit at the clear depth "
                       "1.0, i.e. no occluder was rasterized into the shadow map")
        out.append(_anomaly(
            "empty-shadow-map", "error", SHADOW_MAP_LABEL, finding,
            "shadow pass wrote nothing: check DrawItems reach the shadow pass, fitShadowOrtho "
            "inputs, and winding/cull interaction"))
    return out


# --- Uniform availability: the gate in front of checks 3, 4, 6 and 7 ---------------------------

def _uniform_decode_note(uploads: list, uniforms: dict, schema) -> str | None:
    """The reason nothing could be decoded, or None when uniform data is usable.

    "Nothing was decoded" splits two ways and the split matters: if the *schema* recorded no
    uploads, the frame genuinely uploaded nothing and check 6's error is the right answer; if the
    schema recorded uploads but none came back decoded, the page-attribution policy declined (or
    a decode failed) and none of the uniform checks can say anything at all. Only the second is
    "unavailable" -- and the presence of decoded uploads is a sharper test of it than pattern-
    matching the attribution string, which uniformlib is free to reword.
    """
    if uploads:
        return None
    if not schema.frame_data_uploads:
        return None
    return (uniforms or {}).get("pageAttribution") or "no page attribution recorded"


def _check_uniform_decode_unavailable(note: str) -> list:
    return [_anomaly(
        "uniform-decode-unavailable", "info", "uniforms",
        f"uniform decode unavailable: {note}",
        "the uniform-dependent checks (nan-uniform, degenerate-matrix, no-pass-uniforms-upload, "
        "shadow-transform-mismatch) were skipped, not passed -- re-capture if you need them")]


# --- Check 3: nan-uniform ----------------------------------------------------------------------

def _check_nan_uniform(uploads: list) -> list:
    """One anomaly per distinct `Struct.field`, listing every upload index it was non-finite in.

    Per field rather than per upload because that is the granularity a reader acts on, and deduped
    across uploads because the same field going NaN in eight uploads of one frame is one bug, not
    eight -- a per-upload list would bury the other checks under noise.
    """
    offenders: dict = {}
    for upload in uploads:
        struct_name = upload.get("structName", "unknown")
        index = upload.get("index")
        named_any = False
        for field, value in (upload.get("values") or {}).items():
            if _has_none(value):
                offenders.setdefault(f"{struct_name}.{field}", []).append(index)
                named_any = True
        # `finite` false with no null found means the two disagree -- report the upload rather
        # than silently trusting either, since one of them is lying about the same bytes.
        if upload.get("finite") is False and not named_any:
            offenders.setdefault(f"{struct_name} (field unidentified)", []).append(index)

    return [
        _anomaly("nan-uniform", "error", subject,
                 f"{subject} decoded to a non-finite value (NaN or Inf) in upload(s) "
                 + ", ".join(str(i) for i in indices),
                 "a NaN in a uniform poisons everything downstream of it -- trace back to what "
                 "produced this field on the CPU (a degenerate camera/light basis or an "
                 "uninitialized scene value are the usual sources)")
        for subject, indices in sorted(offenders.items())
    ]


# --- Check 4: degenerate-matrix ----------------------------------------------------------------

def _check_degenerate_matrix(uploads: list) -> list:
    """A collapsed viewProj or shadowTransform, deduped by `Struct.field` like check 3.

    The determinant is computed on the decoded row-major matrix without transposing first, which
    is correct because det(M) == det(M^T) -- the one place in this file where the row/column
    convention genuinely does not matter (contrast check 7, where it decides the answer).
    """
    offenders: dict = {}
    for upload in uploads:
        struct_name = upload.get("structName", "unknown")
        index = upload.get("index")
        for field in _MATRIX_FIELDS:
            matrix = (upload.get("values") or {}).get(field)
            if not isinstance(matrix, list) or len(matrix) != 4 or _has_none(matrix):
                continue  # absent, malformed, or check 3's problem
            all_zero = all(value == 0.0 for row in matrix for value in row)
            det = shadowmath.determinant(matrix)
            if all_zero or abs(det) < _DEGENERATE_DET:
                reason = "every entry is zero" if all_zero else f"its determinant is {det:.3e}"
                offenders.setdefault(f"{struct_name}.{field}", []).append((index, reason))

    out = []
    for subject, hits in sorted(offenders.items()):
        indices = ", ".join(str(index) for index, _ in hits)
        out.append(_anomaly(
            "degenerate-matrix", "error", subject,
            f"{subject} is degenerate in upload(s) {indices}: {hits[0][1]}",
            "a singular matrix collapses geometry to a point or a plane -- check the scene's "
            "bounding sphere radius, the light direction, and the camera's near/far planes for a "
            "zero or a NaN that got squashed to one"))
    return out


# --- Check 5: missing-expected-resource --------------------------------------------------------

def _check_missing_expected_resource(resources: dict, schema) -> list:
    schema_labels = {r.label for r in getattr(schema, "resources", [])}
    missing_labels = {entry["label"] for entry in resources.get("missingResources", [])}
    undecodable = _undecodable_reasons(resources)

    out = []
    for label in sorted(EXPECTED_LABELS):
        if label not in schema_labels:
            out.append(_anomaly(
                "missing-expected-resource", "error", label,
                f"{label} is absent from the schema sidecar's resource list",
                "never created: the resource did not exist when the sidecar was written -- check "
                "that the renderer got as far as creating it, and that the sidecar isn't stale"))
        elif label in missing_labels:
            out.append(_anomaly(
                "missing-expected-resource", "error", label,
                f"{label} is in the schema but no label record for it was found in the bundle",
                "not in the bundle: the resource existed but the capture never recorded it -- "
                "check the capture actually spanned the frame that used it"))
        elif undecodable.get(label) == _NO_CONTENTS:
            out.append(_anomaly(
                "missing-expected-resource", "error", label,
                f"{label} is labelled in the bundle but no blob file was dumped for it",
                "not in the bundle: a missing blob is normal for fallback textures and higher "
                f"sky mips, but {label} is dumped in every healthy capture -- re-capture and "
                "check the resource was still resident at endCapture()"))
    return out


# --- Check 6: no-pass-uniforms-upload ----------------------------------------------------------

def _check_no_pass_uniforms_upload(uploads: list) -> list:
    """Matched on `structName`, deliberately not on slot: the sky's SkyUniforms binds the same
    slot 2, so a slot-based count would report "PassUniforms was uploaded" for a frame that only
    ever drew the sky."""
    if any(u.get("structName") == "PassUniforms" for u in uploads):
        return []
    return [_anomaly(
        "no-pass-uniforms-upload", "error", "PassUniforms",
        f"no PassUniforms upload was recorded in this capture ({len(uploads)} upload(s) total)",
        "scene pass never uploaded PassUniforms -- was the frame captured mid-scene-switch?")]


# --- Check 7: shadow-transform-mismatch --------------------------------------------------------

def _last_pass_uniforms(uploads: list):
    latest = None
    for upload in uploads:
        if upload.get("structName") == "PassUniforms":
            latest = upload
    return latest


def _finite_floats(values) -> bool:
    return isinstance(values, list) and values and not _has_none(values)


def _skipped_cross_check(reason: str) -> list:
    return [_anomaly(
        "shadow-transform-mismatch", "info", "PassUniforms.shadowTransform",
        f"shadow-transform cross-check skipped -- cannot recompute: {reason}",
        "this is a gap in what the dump could verify, not a finding about the frame")]


def _check_shadow_transform(context: dict, uploads: list) -> list:
    upload = _last_pass_uniforms(uploads)
    if upload is None:
        return []  # check 6 already said the sharper thing
    uploaded = (upload.get("values") or {}).get("shadowTransform")
    if not isinstance(uploaded, list) or len(uploaded) != 4 or _has_none(uploaded):
        return []  # absent or non-finite: checks 3/4 own it

    sphere = context.get("boundingSphere")
    light_dir = context.get("light0Direction")
    if not _finite_floats(sphere) or len(sphere) != 4:
        return _skipped_cross_check("the sidecar's context has no finite boundingSphere "
                                    "(a non-finite float is written as JSON null)")
    if not _finite_floats(light_dir) or len(light_dir) != 3:
        return _skipped_cross_check("the sidecar's context has no finite light0Direction "
                                    "(a non-finite float is written as JSON null)")
    try:
        expected = shadowmath.fit_shadow_ortho(sphere, light_dir)[1]
    except ValueError as exc:
        return _skipped_cross_check(str(exc))

    # THE TRANSPOSE. `expected` is shadowmath's column-major storage (`expected[col][row]`, glm's
    # convention); `uploaded` is uniformlib's row-major *display* shape (`uploaded[row][col]`).
    # Same matrix, two shapes -- so the comparison indexes them with swapped subscripts rather
    # than transposing either one into a new list. Getting this backwards fires on every healthy
    # capture, which is what test_does_not_fire_when_the_upload_matches_the_recompute pins.
    worst = 0.0
    worst_at = None
    for row in range(4):
        for col in range(4):
            want = expected[col][row]
            got = uploaded[row][col]
            delta = abs(got - want)
            if delta > _MATRIX_REL_TOLERANCE * max(1.0, abs(want)) and delta > worst:
                worst, worst_at = delta, (row, col, want, got)

    if worst_at is None:
        return []
    row, col, want, got = worst_at
    return [_anomaly(
        "shadow-transform-mismatch", "error", "PassUniforms.shadowTransform",
        f"the uploaded shadowTransform disagrees with a recompute from the sidecar's scene state "
        f"(boundingSphere {sphere}, light0Direction {light_dir}); largest disagreement at "
        f"row {row} col {col}: uploaded {got!r} vs recomputed {want!r} (delta {worst:.6g})",
        "uploaded shadowTransform disagrees with a recompute from the sidecar's scene state: "
        "stale upload, wrong slot, or fitShadowOrtho input drift")]


# --- Check 8: unmatched-blobs ------------------------------------------------------------------

def _check_unmatched_blobs(resources: dict) -> list:
    blobs = resources.get("unmatchedBlobs", [])
    if not blobs:
        return []
    total = sum(blob.get("sizeBytes", 0) for blob in blobs)
    return [_anomaly(
        "unmatched-blobs", "info", "bundle",
        f"{len(blobs)} blob file(s) totalling {total} bytes were reached by no label record, so "
        "nothing in the manifest describes them",
        "usually harmless (the drawable and stray buffers land here) -- worth a look only if a "
        "resource you expected is also missing, since the two together mean a label join failed")]


# --- Check 9: undecodable-resource -------------------------------------------------------------

def _check_undecodable_resources(resources: dict) -> list:
    """Surface the matchedUndecodable reasons that represent real damage.

    Header/schema disagreement, unrecognized formats, and inconsistent headers represent real
    damage. The two expected reasons are excluded: missing capture contents and BC1 compression.
    """
    out = []
    for label, reason in sorted(_undecodable_reasons(resources).items()):
        if reason == _NO_CONTENTS or _BC1_MARKER in reason:
            continue
        out.append(_anomaly(
            "undecodable-resource", "warning", label,
            f"{label} was matched to a blob but could not be decoded: {reason}",
            "the blob header is the authority on geometry and format, so a disagreement means "
            "either the sidecar is stale relative to the binary that captured, or the blob is "
            "not the one the label join thought it was"))
    return out


# --- Entry point -------------------------------------------------------------------------------

def run_checks(manifest: dict, uniforms: dict, schema) -> list:
    """Every check, in the plan's numbered order, severity-ordered on the way out.

    `manifest` is the dict `gputrace_dump.build_manifest` produced (images + stats, resource
    buckets, capture context); `uniforms` is the whole `uniforms.json` document rather than just
    its `uploads` list, because the page-attribution note that sits beside them is what tells the
    uniform checks whether a silence means "nothing was uploaded" or "nothing could be read";
    `schema` is the `schemalib.Schema`.

    The final sort is by severity only, and Python's sort is stable, so within a severity the
    order is still "check 1's findings, then check 2's, ... , each sorted by subject".
    """
    images = manifest.get("images") or []
    resources = manifest.get("resources") or {}
    context = manifest.get("capture") or {}
    uploads = (uniforms or {}).get("uploads") or []

    anomalies = []
    anomalies += _check_flat_image(images)
    anomalies += _check_empty_shadow_map(images)

    unavailable = _uniform_decode_note(uploads, uniforms, schema)
    if unavailable is not None:
        anomalies += _check_uniform_decode_unavailable(unavailable)
    else:
        anomalies += _check_nan_uniform(uploads)
        anomalies += _check_degenerate_matrix(uploads)

    anomalies += _check_missing_expected_resource(resources, schema)

    if unavailable is None:
        anomalies += _check_no_pass_uniforms_upload(uploads)
        anomalies += _check_shadow_transform(context, uploads)

    anomalies += _check_unmatched_blobs(resources)
    anomalies += _check_undecodable_resources(resources)

    return sorted(anomalies, key=lambda a: _SEVERITY_RANK[a["severity"]])
