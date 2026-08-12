"""Unit tests for bundlelib.py.

Fixtures mirror the bundle shape measured from real macOS 26 captures:
blobs live at the bundle root, labels live only in *device-resources-* streams, and label->blob
association goes through a shared receiver handle -- never through blob-file contents.

Run: python3 -m unittest discover -s Tools/GpuDebug/tests -v
"""
import pathlib
import plistlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

import bundlelib  # noqa: E402
import schemalib  # noqa: E402


# --- Byte-level fixture helpers -----------------------------------------------------------
#
# These reproduce the measured on-disk record framing: a NUL-terminated
# ASCII type descriptor at a 4-byte-aligned offset, alignment padding, an 8-byte receiver handle,
# then the payload. Every record here is itself padded to a 4-byte multiple so concatenating
# records keeps every subsequent descriptor 4-byte aligned, exactly like the real stream.

def _pack_record(descriptor: bytes, receiver: int, payload: bytes) -> bytes:
    head = descriptor + b"\x00"
    head += b"\x00" * ((-len(head)) % 4)
    body = head + struct.pack("<Q", receiver) + payload + b"\x00"
    body += b"\x00" * ((-len(body)) % 4)
    return body


def _cs_and_blob_records(receiver: int, label: bytes, blob_name: bytes) -> bytes:
    """A CS (setLabel:) record and a <b> (blob-naming) record sharing one receiver handle."""
    return (_pack_record(b"CS", receiver, label)
            + _pack_record(b"C<b>", receiver, blob_name))


def _texture_header(width: int, height: int, pixel_format: int = 80,
                    version: int = bundlelib.EXPECTED_HEADER_VERSION) -> bytes:
    """A 256-byte capture-blob header."""
    bytes_per_row = width * 4
    bytes_per_image = bytes_per_row * height
    header = bundlelib.BLOB_HEADER_MAGIC
    header += struct.pack("<II", version, bundlelib.BLOB_HEADER_SIZE)
    header += struct.pack("<QQQQQQQ", 1, pixel_format, width, height, 1, bytes_per_row,
                          bytes_per_image)
    header += b"\x00" * (bundlelib.BLOB_HEADER_SIZE - len(header))
    return header


class BundleFixture:
    """Build a fixture shaped like a real capture bundle."""

    def __init__(self, root: pathlib.Path):
        self.root = root
        self._next_receiver = 0xDEAD0000

    def _receiver(self) -> int:
        self._next_receiver += 1
        return self._next_receiver

    def write_metadata(self):
        (self.root / "metadata").write_bytes(plistlib.dumps({"deviceName": "test"}))

    def write_texture_blob(self, resource_id: int, width: int, height: int,
                           pixel_format: int = 80, payload: bytes = b"") -> str:
        name = f"MTLTexture-{resource_id}-0-mipmap0-slice0"
        return self.write_texture_blob_named(name, width, height, pixel_format)

    def write_texture_blob_named(self, name: str, width: int, height: int,
                                 pixel_format: int = 80) -> str:
        """Like write_texture_blob, but for an arbitrary mip/slice filename -- e.g. a partial
        capture where only a higher mip exists. Always writes a real (correctly-sized) payload,
        not just the bare 256-byte header, so the header's bytesPerImage plausibility check
        passes for a geometry-correct blob, exactly like a real bundle."""
        header = _texture_header(width, height, pixel_format)
        payload = b"\x00" * (width * 4 * height)  # matches _texture_header's bytesPerImage calc
        data = header + payload
        # File sizes are padded to a 16 KiB multiple on real bundles; pad here too so a decoder
        # that (wrongly) trusts file size instead of the header is exercised, not just skipped.
        pad = (-len(data)) % (16 * 1024)
        (self.root / name).write_bytes(data + b"\x00" * pad)
        return name

    def write_buffer_blob(self, resource_id: int, contents: bytes) -> str:
        name = f"MTLBuffer-{resource_id}-0"
        (self.root / name).write_bytes(contents)
        return name

    def add_labelled_resource(self, label: str, blob_name: str | None) -> int:
        """Registers one (CS, <b>) record pair sharing a fresh receiver; returns the receiver."""
        receiver = self._receiver()
        content = _pack_record(b"CS", receiver, label.encode("ascii"))
        if blob_name is not None:
            content += _pack_record(b"C<b>", receiver, blob_name.encode("ascii"))
        self._pending = getattr(self, "_pending", b"") + content
        return receiver

    def flush_device_resources(self, filename: str = "device-resources-0xdead") -> pathlib.Path:
        """`filename` must carry one of the three device-resources prefixes verbatim -- the
        "unused"/"delta" markers are PREFIXES on the real files (unused-device-resources-0x...),
        not suffixes, so callers must not just tack a marker onto "device-resources-"."""
        path = self.root / filename
        path.write_bytes(getattr(self, "_pending", b""))
        self._pending = b""
        return path


def _make_fixture(root: pathlib.Path) -> BundleFixture:
    return BundleFixture(root)


class WalkBundleTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = pathlib.Path(self._tmp.name) / "test.gputrace"
        self.root.mkdir()

    def test_missing_bundle_raises_bundle_error_with_run_hint(self):
        missing = pathlib.Path(self._tmp.name) / "nope.gputrace"
        with self.assertRaises(bundlelib.BundleError) as ctx:
            bundlelib.walk_bundle(missing)
        self.assertIn("LMX_CAPTURE_PATH", str(ctx.exception))

    def test_bundle_without_device_resources_raises(self):
        (self.root / "metadata").write_bytes(plistlib.dumps({}))
        with self.assertRaises(bundlelib.BundleError) as ctx:
            bundlelib.walk_bundle(self.root)
        self.assertIn("device-resources", str(ctx.exception))

    def test_corrupt_metadata_plist_raises_with_run_hint(self):
        # Every fatal path must carry the command that produces a fresh artifact.
        (self.root / "metadata").write_bytes(b"not a plist")
        fx = _make_fixture(self.root)
        fx.add_labelled_resource("lmx.unrelated", None)
        fx.flush_device_resources()

        with self.assertRaises(bundlelib.BundleError) as ctx:
            bundlelib.walk_bundle(self.root)
        self.assertIn("LMX_CAPTURE_PATH", str(ctx.exception))

    def test_finds_metadata_blobs_and_device_resources(self):
        fx = _make_fixture(self.root)
        fx.write_metadata()
        fx.write_texture_blob(1, width=4, height=2)
        fx.write_buffer_blob(99, b"\x00" * 16)
        fx.add_labelled_resource("lmx.test.tex", "MTLTexture-1-0-mipmap0-slice0")
        fx.flush_device_resources()

        bundle = bundlelib.walk_bundle(self.root)

        self.assertEqual(bundle.metadata["deviceName"], "test")
        blob_names = {b.path.name for b in bundle.blobs}
        self.assertIn("MTLTexture-1-0-mipmap0-slice0", blob_names)
        self.assertIn("MTLBuffer-99-0", blob_names)
        self.assertEqual(len(bundle.device_resource_files), 1)

    def test_blob_size_bytes_matches_file_size(self):
        fx = _make_fixture(self.root)
        fx.add_labelled_resource("lmx.test.tex", "MTLTexture-1-0-mipmap0-slice0")
        fx.flush_device_resources()
        name = fx.write_texture_blob(1, width=4, height=2)

        bundle = bundlelib.walk_bundle(self.root)

        blob = next(b for b in bundle.blobs if b.path.name == name)
        self.assertEqual(blob.size_bytes, (self.root / name).stat().st_size)


class ScanLabelsTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = pathlib.Path(self._tmp.name) / "test.gputrace"
        self.root.mkdir()

    def test_finds_the_planted_label(self):
        fx = _make_fixture(self.root)
        fx.write_texture_blob(1, width=4, height=2)
        fx.add_labelled_resource("lmx.test.tex", "MTLTexture-1-0-mipmap0-slice0")
        fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)

        hits = bundlelib.scan_labels(bundle)

        self.assertEqual(len(hits), 1)
        self.assertEqual(hits[0].label, "lmx.test.tex")
        self.assertEqual(hits[0].resource_id_prefix, "MTLTexture-1")

    def test_does_not_truncate_a_label_whose_suffix_looks_like_a_descriptor(self):
        # A measured failure mode clipped "lmx.render.sceneColor" to
        # "lmx.render.scene" because "Color\0" is itself a well-formed, 4-byte-aligned
        # descriptor. Guard this exact case: read the full NUL-terminated string, not "up to the
        # next descriptor-shaped match".
        fx = _make_fixture(self.root)
        fx.write_texture_blob(7, width=4, height=2)
        fx.add_labelled_resource("lmx.render.sceneColor", "MTLTexture-7-0-mipmap0-slice0")
        fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)

        hits = bundlelib.scan_labels(bundle)

        labels = [h.label for h in hits]
        self.assertIn("lmx.render.sceneColor", labels)
        self.assertNotIn("lmx.render.scene", labels)

    def test_scans_all_three_device_resources_streams(self):
        # Some labels (e.g. lmx.imgui.formatCarrier) only appear in the
        # unused-device-resources stream -- scan_labels must not stop at the first file found.
        fx = _make_fixture(self.root)
        fx.write_texture_blob(1, width=4, height=2)
        fx.write_texture_blob(2, width=8, height=8)
        fx.add_labelled_resource("lmx.primary.tex", "MTLTexture-1-0-mipmap0-slice0")
        fx.flush_device_resources("device-resources-0xaaa")
        fx.add_labelled_resource("lmx.unused.tex", "MTLTexture-2-0-mipmap0-slice0")
        fx.flush_device_resources("unused-device-resources-0xaaa")
        bundle = bundlelib.walk_bundle(self.root)

        hits = bundlelib.scan_labels(bundle)

        labels = {h.label for h in hits}
        self.assertIn("lmx.primary.tex", labels)
        self.assertIn("lmx.unused.tex", labels)

    def test_label_with_no_blob_record_produces_no_hit(self):
        fx = _make_fixture(self.root)
        fx.add_labelled_resource("lmx.orphan.label", None)
        fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)

        hits = bundlelib.scan_labels(bundle)

        self.assertEqual(hits, [])


def _schema_with(resources) -> schemalib.Schema:
    return schemalib.Schema(context={}, resources=resources, uniform_structs=[],
                            frame_data_uploads=[])


def _texture_resource(label, width, height, fmt="BGRA8Unorm", mip_levels=1) -> schemalib.Resource:
    return schemalib.Resource(label=label, kind="texture2d", format=fmt, width=width,
                              height=height, mip_levels=mip_levels, size_bytes=None)


def _buffer_resource(label, size_bytes) -> schemalib.Resource:
    return schemalib.Resource(label=label, kind="buffer", format=None, width=None, height=None,
                              mip_levels=None, size_bytes=size_bytes)


class JoinTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = pathlib.Path(self._tmp.name) / "test.gputrace"
        self.root.mkdir()
        self.fx = _make_fixture(self.root)

    def test_matches_labelled_resource_to_its_blob(self):
        self.fx.write_texture_blob(1, width=4, height=2)
        self.fx.add_labelled_resource("lmx.test.tex", "MTLTexture-1-0-mipmap0-slice0")
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([_texture_resource("lmx.test.tex", 4, 2)])

        result = bundlelib.join(bundle, schema, hits)

        self.assertEqual(len(result.matched), 1)
        resource, blob = result.matched[0]
        self.assertEqual(resource.label, "lmx.test.tex")
        self.assertEqual(blob.path.name, "MTLTexture-1-0-mipmap0-slice0")
        self.assertEqual(result.unmatched_blobs, [])
        self.assertEqual(result.missing_resources, [])

    def test_orphan_blob_with_no_record_lands_in_unmatched_blobs(self):
        self.fx.write_texture_blob(1, width=4, height=2)  # never referenced by any record
        self.fx.add_labelled_resource("lmx.unrelated", None)  # needs >=1 device-resources record
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([])

        result = bundlelib.join(bundle, schema, hits)

        blob_names = {b.path.name for b in result.unmatched_blobs}
        self.assertIn("MTLTexture-1-0-mipmap0-slice0", blob_names)

    def test_schema_only_resource_lands_in_missing_resources(self):
        self.fx.add_labelled_resource("lmx.unrelated", None)
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([_texture_resource("lmx.render.shadowMap", 2048, 2048,
                                                  fmt="D32Float")])

        result = bundlelib.join(bundle, schema, hits)

        labels = {r.label for r in result.missing_resources}
        self.assertIn("lmx.render.shadowMap", labels)

    def test_resource_with_record_but_no_blob_is_no_contents_captured(self):
        # A referenced blob name may have no file on disk. Normal, not an
        # anomaly -- bucket as matched_undecodable with an explicit reason, exit 0.
        self.fx.add_labelled_resource("lmx.render.whiteFallback",
                                      "MTLTexture-42-0-mipmap0-slice0")
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([_texture_resource("lmx.render.whiteFallback", 4, 4)])

        result = bundlelib.join(bundle, schema, hits)

        self.assertEqual(len(result.matched_undecodable), 1)
        resource, reason = result.matched_undecodable[0]
        self.assertEqual(resource.label, "lmx.render.whiteFallback")
        self.assertEqual(reason, "no contents captured")

    def test_geometry_mismatch_buckets_as_matched_undecodable_not_missing(self):
        # The header wins over the schema, and a disagreement is a real anomaly -- not a match
        # failure indistinguishable from "no
        # record at all". It must land in matched_undecodable with a distinct reason, and the
        # blob stays unclaimed (never mislabeled into `matched`).
        self.fx.write_texture_blob(3, width=4, height=2)
        self.fx.add_labelled_resource("lmx.mismatched", "MTLTexture-3-0-mipmap0-slice0")
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([_texture_resource("lmx.mismatched", 64, 64)])

        result = bundlelib.join(bundle, schema, hits)

        self.assertEqual(result.matched, [])
        self.assertEqual(result.missing_resources, [])
        self.assertEqual(len(result.matched_undecodable), 1)
        resource, reason = result.matched_undecodable[0]
        self.assertEqual(resource.label, "lmx.mismatched")
        self.assertEqual(reason, "header geometry disagrees with schema")
        blob_names = {b.path.name for b in result.unmatched_blobs}
        self.assertIn("MTLTexture-3-0-mipmap0-slice0", blob_names)

    def test_bytes_per_image_exceeding_file_size_is_matched_undecodable(self):
        # The header's bytesPerImage must actually fit inside
        # the blob file on disk (file sizes are padded to 16 KiB, but a header claiming MORE
        # payload than the file holds is a real anomaly, not a match). The header itself must
        # still be the full 256 bytes -- otherwise parse_blob_header returns None and the
        # geometry/plausibility check is skipped entirely rather than exercised.
        blob_path = self.root / "MTLTexture-6-0-mipmap0-slice0"
        header = _texture_header(4, 2)  # bytesPerImage = 16 * 2 = 32; header claims 256 + 32
        blob_path.write_bytes(header + b"\x00" * 10)  # only 10 of the 32 payload bytes present
        self.fx.add_labelled_resource("lmx.truncated", "MTLTexture-6-0-mipmap0-slice0")
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([_texture_resource("lmx.truncated", 4, 2)])

        result = bundlelib.join(bundle, schema, hits)

        self.assertEqual(result.matched, [])
        self.assertEqual(len(result.matched_undecodable), 1)
        resource, reason = result.matched_undecodable[0]
        self.assertEqual(resource.label, "lmx.truncated")
        self.assertEqual(reason, "header bytesPerImage exceeds the blob file size")

    def test_partial_capture_with_only_a_higher_mip_still_matches(self):
        # When mipmap0-slice0 was not captured (normal for a
        # partial capture, e.g. the sky cubemap's higher mips), the primary-blob fallback must
        # not false-reject by comparing a higher mip's header against the level-0 schema dims.
        base_width, base_height, mip_level = 64, 64, 2
        expected_width, expected_height = base_width >> mip_level, base_height >> mip_level  # 16
        name = f"MTLTexture-8-0-mipmap{mip_level}-slice0"
        self.fx.write_texture_blob_named(name, expected_width, expected_height)
        self.fx.add_labelled_resource("lmx.partialMip", name)
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([_texture_resource("lmx.partialMip", base_width, base_height,
                                                  mip_levels=4)])

        result = bundlelib.join(bundle, schema, hits)

        self.assertEqual(result.missing_resources, [])
        self.assertEqual(result.matched_undecodable, [])
        matched_names = {blob.path.name for _, blob in result.matched}
        self.assertIn(name, matched_names)

    def test_primary_blob_selection_is_numeric_not_lexicographic(self):
        # "mipmap10-slice0" sorts before "mipmap2-slice0" as a string. Without numeric sorting,
        # the primary-blob fallback would pick mip 10 (when only 2 and 10 exist, no mip 0) and
        # validate against the wrong expected dimensions.
        base = 64
        self.fx.write_texture_blob_named("MTLTexture-11-0-mipmap10-slice0", 1, 1)
        expected = base >> 2
        self.fx.write_texture_blob_named("MTLTexture-11-0-mipmap2-slice0", expected, expected)
        self.fx.add_labelled_resource("lmx.manyMips", "MTLTexture-11-0-mipmap2-slice0")
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([_texture_resource("lmx.manyMips", base, base, mip_levels=11)])

        result = bundlelib.join(bundle, schema, hits)

        # If the primary pick were mip 10 (1x1) compared against the un-shifted schema dims (or
        # even against 64>>10 rounding to 0), this would false-reject into matched_undecodable.
        self.assertEqual(result.matched_undecodable, [])
        matched_names = {blob.path.name for _, blob in result.matched}
        self.assertIn("MTLTexture-11-0-mipmap2-slice0", matched_names)
        self.assertIn("MTLTexture-11-0-mipmap10-slice0", matched_names)

    def test_one_resource_maps_to_many_mip_slice_blobs(self):
        self.fx.write_texture_blob(5, width=4, height=4)
        name0 = "MTLTexture-5-0-mipmap0-slice0"
        (self.root / "MTLTexture-5-0-mipmap1-slice0").write_bytes(_texture_header(2, 2))
        self.fx.add_labelled_resource("lmx.mipped", name0)
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([_texture_resource("lmx.mipped", 4, 4, mip_levels=2)])

        result = bundlelib.join(bundle, schema, hits)

        matched_names = {blob.path.name for _, blob in result.matched}
        self.assertIn("MTLTexture-5-0-mipmap0-slice0", matched_names)
        self.assertIn("MTLTexture-5-0-mipmap1-slice0", matched_names)

    def test_buffer_matches_by_unambiguous_size(self):
        self.fx.write_buffer_blob(13, b"\xab" * 64)
        self.fx.add_labelled_resource("lmx.unrelated", None)
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([_buffer_resource("lmx.device.frameData.0.page.0", 64)])

        result = bundlelib.join(bundle, schema, hits)

        self.assertEqual(len(result.matched), 1)
        resource, blob = result.matched[0]
        self.assertEqual(resource.label, "lmx.device.frameData.0.page.0")
        self.assertEqual(blob.path.name, "MTLBuffer-13-0")

    def test_ambiguous_buffer_size_stays_unmatched(self):
        self.fx.write_buffer_blob(1, b"\x00" * 32)
        self.fx.write_buffer_blob(2, b"\x00" * 32)
        self.fx.add_labelled_resource("lmx.unrelated", None)
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([_buffer_resource("lmx.device.frameData.0.page.0", 32)])

        result = bundlelib.join(bundle, schema, hits)

        self.assertEqual(result.matched, [])
        labels = {r.label for r in result.missing_resources}
        self.assertIn("lmx.device.frameData.0.page.0", labels)

    def test_buffer_resource_with_no_matching_blob_is_no_contents_captured(self):
        self.fx.add_labelled_resource("lmx.unrelated", None)
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([_buffer_resource("lmx.device.frameData.0.page.0", 999)])

        result = bundlelib.join(bundle, schema, hits)

        self.assertEqual(len(result.matched_undecodable), 1)
        resource, reason = result.matched_undecodable[0]
        self.assertEqual(resource.label, "lmx.device.frameData.0.page.0")
        self.assertEqual(reason, "no contents captured")

    def test_three_same_size_buffer_resources_one_blob_never_mislabels(self):
        # A real App capture can contain three same-size frame-data page resources (one per
        # frame-in-flight slot), exactly one plausibly-sized blob on disk. The ambiguity must be
        # judged on the RESOURCE side too, not just the blob side: with only blob-count
        # considered, frameData.0.page.0 would hit len(candidates) == 1 and claim the blob as a
        # confident (and arbitrary, and possibly wrong) match. None of the three may match.
        self.fx.write_buffer_blob(20, b"\xcd" * 262144)
        self.fx.add_labelled_resource("lmx.unrelated", None)
        self.fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)
        hits = bundlelib.scan_labels(bundle)
        schema = _schema_with([
            _buffer_resource("lmx.device.frameData.0.page.0", 262144),
            _buffer_resource("lmx.device.frameData.0.page.1", 262144),
            _buffer_resource("lmx.device.frameData.0.page.2", 262144),
        ])

        result = bundlelib.join(bundle, schema, hits)

        self.assertEqual(result.matched, [])
        missing_labels = {r.label for r in result.missing_resources}
        self.assertEqual(missing_labels, {"lmx.device.frameData.0.page.0", "lmx.device.frameData.0.page.1",
                                          "lmx.device.frameData.0.page.2"})
        blob_names = {b.path.name for b in result.unmatched_blobs}
        self.assertIn("MTLBuffer-20-0", blob_names)


class ParseBlobHeaderTests(unittest.TestCase):
    def test_parses_known_fields(self):
        data = _texture_header(width=300, height=299, pixel_format=80)

        header = bundlelib.parse_blob_header(data)

        self.assertEqual(header.version, bundlelib.EXPECTED_HEADER_VERSION)
        self.assertEqual(header.header_size, 256)
        self.assertEqual(header.pixel_format, 80)
        self.assertEqual(header.width, 300)
        self.assertEqual(header.height, 299)
        self.assertEqual(header.bytes_per_row, 1200)
        self.assertEqual(header.bytes_per_image, 1200 * 299)

    def test_returns_none_for_non_blob_data(self):
        self.assertIsNone(bundlelib.parse_blob_header(b"\x00" * 256))
        self.assertIsNone(bundlelib.parse_blob_header(b"too short"))


class FirstTextureHeaderVersionTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = pathlib.Path(self._tmp.name) / "test.gputrace"
        self.root.mkdir()

    def test_reads_version_from_first_texture_blob(self):
        fx = _make_fixture(self.root)
        fx.write_texture_blob(1, width=4, height=2)
        fx.add_labelled_resource("lmx.test.tex", "MTLTexture-1-0-mipmap0-slice0")
        fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)

        version = bundlelib.first_texture_header_version(bundle)

        self.assertEqual(version, bundlelib.EXPECTED_HEADER_VERSION)

    def test_none_when_no_texture_blob_has_a_header(self):
        fx = _make_fixture(self.root)
        fx.write_buffer_blob(1, b"\x00" * 16)
        fx.add_labelled_resource("lmx.unrelated", None)
        fx.flush_device_resources()
        bundle = bundlelib.walk_bundle(self.root)

        self.assertIsNone(bundlelib.first_texture_header_version(bundle))


if __name__ == "__main__":
    unittest.main()
