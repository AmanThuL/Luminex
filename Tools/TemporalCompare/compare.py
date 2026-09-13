#!/usr/bin/env python3
"""Capture and compare aligned temporal reconstructions without a quality ground truth."""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import math
import os
from pathlib import Path
import subprocess
import sys

MODES = ("raw", "taa", "metalfx")
LABELS = {"raw": "Raw spatial", "taa": "Native TAA / TAAU", "metalfx": "MetalFX"}
COMMON = ("scene", "width", "height", "fps", "warmup", "frameCount", "renderScale",
          "debugView", "cameraTrack", "dynamicResolution", "device")
FRAME_COMMON = ("ordinal", "simulationFrame", "timeSeconds", "camera", "renderWidth", "renderHeight",
                "effectiveScale", "jitterIndex", "jitterEnabled", "exposureEv", "autoExposure",
                "bloom", "bloomThreshold", "bloomIntensity", "shadowFilter")


def sha256(path: Path) -> str:
    """Hash exact inputs and output images for a relocatable provenance record."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_manifests(root: Path) -> dict:
    """Reject partial, misaligned, clamped, or silently substituted comparisons."""
    manifests = {}
    for mode in MODES:
        path = root / mode / "manifest.json"
        data = json.loads(path.read_text())
        if data.get("schemaVersion") not in (1, 2) or data.get("complete") is not True:
            raise ValueError(f"{mode}: incomplete or unsupported capture manifest")
        if data.get("requestedMode") != mode or data.get("debugView") != 0:
            raise ValueError(f"{mode}: wrong requested mode or diagnostic overlay")
        if data.get("dynamicResolution") is not False:
            raise ValueError(f"{mode}: expected fixed-scale sRGB LDR capture")
        if data["schemaVersion"] == 1:
            if data.get("colorSpace") != "sRGB LDR":
                raise ValueError(f"{mode}: unsupported colorSpace={data.get('colorSpace')!r}")
        else:
            display = data.get("display", {})
            if not isinstance(display, dict):
                raise ValueError(f"{mode}: invalid display={display!r}")
            view, transfer = display.get("view"), display.get("transfer")
            if view != "sdr" or transfer != "srgb":
                raise ValueError(f"{mode}: unsupported display view={view!r}, transfer={transfer!r}")
            if data.get("container") not in ("png", "bmp"):
                raise ValueError(f"{mode}: unsupported container={data.get('container')!r}")
            if data.get("ui") != {"composited": False}:
                raise ValueError(f"{mode}: expected an offscreen capture without composited UI")
        frames = data.get("frames", [])
        if not frames or len(frames) != data.get("frameCount"):
            raise ValueError(f"{mode}: saved frame count mismatch")
        filenames = set()
        for index, frame in enumerate(frames):
            if frame.get("ordinal") != index or frame.get("simulationFrame") != data["warmup"] + index:
                raise ValueError(f"{mode}: frame numbering is not contiguous")
            if not math.isclose(frame["timeSeconds"], frame["simulationFrame"] / data["fps"], abs_tol=1e-12):
                raise ValueError(f"{mode}: simulation time mismatch")
            if frame.get("effectiveMode") != mode or frame.get("fallback") != 0:
                raise ValueError(f"{mode}: fallback/substituted reconstruction refused")
            if frame.get("autoExposure") is not False or frame.get("jitterEnabled") is not True:
                raise ValueError(f"{mode}: comparison requires manual exposure and jitter")
            if not math.isclose(frame["effectiveScale"], data["renderScale"], abs_tol=1e-7):
                raise ValueError(f"{mode}: requested render scale was clamped")
            filename = frame.get("file", "")
            if (not isinstance(filename, str) or Path(filename).name != filename
                    or Path(filename).suffix.lower() not in (".bmp", ".png")
                    or not (root / mode / filename).is_file()):
                raise ValueError(f"{mode}: missing or invalid frame filename")
            if filename in filenames:
                raise ValueError(f"{mode}: duplicate frame filename {filename!r}")
            filenames.add(filename)
            if data["schemaVersion"] == 2 and Path(filename).suffix.lower() != "." + data["container"]:
                raise ValueError(f"{mode}: frame container differs from manifest")
        manifests[mode] = data
    baseline = manifests["taa"]
    domains = [(mode, data["display"]) for mode, data in manifests.items()
               if data["schemaVersion"] == 2]
    for mode, domain in domains[1:]:
        if domain != domains[0][1]:
            raise ValueError(f"{mode}: display domain differs from {domains[0][0]}")
    for mode in MODES:
        data = manifests[mode]
        for field in COMMON:
            if data[field] != baseline[field]:
                raise ValueError(f"{mode}: capture setting {field} differs from Native TAA")
        for index, (frame, reference) in enumerate(zip(data["frames"], baseline["frames"])):
            for field in FRAME_COMMON:
                if frame[field] != reference[field]:
                    raise ValueError(f"{mode}: frame {index} {field} differs from Native TAA")
    return manifests


def flip_difference(reference: Path, test: Path, output: Path, ppd: float) -> dict:
    """Compute LDR algorithm difference; Native TAA is a baseline, never ground truth."""
    import numpy as np
    import flip_evaluator as flip
    from PIL import Image
    version = importlib.metadata.version("flip-evaluator")
    if version != "1.7":
        raise ValueError(f"expected pinned flip-evaluator 1.7, got {version}")
    error, mean, parameters = flip.evaluate(str(reference), str(test), "LDR", inputsRGB=True,
                                           applyMagma=False, parameters={"ppd": ppd})
    values = np.asarray(error, dtype=np.float32)
    if values.ndim == 3:
        values = values[:, :, 0]
    if not np.isfinite(values).all() or not math.isfinite(mean):
        raise ValueError("FLIP returned non-finite differences")
    # Fixed 0..1 grayscale preserves a shared scale across every frame and algorithm.
    Image.fromarray(np.rint(np.clip(values, 0, 1) * 255).astype("uint8")).save(output)
    # The API also returns unused HDR defaults (including infinities). Record only the
    # actual LDR viewing parameter so the report remains strict, portable JSON.
    return {"mean": float(mean), "p95": float(np.percentile(values, 95, method="linear")),
            "map": str(output.name), "parameters": {"ppd": parameters["ppd"]}}


def build_report(root: Path, with_flip: bool = False, ppd: float = 67.0) -> Path:
    """Preserve PNG captures, convert BMPs, and embed navigation for file:// playback."""
    from PIL import Image
    manifests = validate_manifests(root)
    baseline = manifests["taa"]
    data = {"schemaVersion": 1, "baseline": "taa", "labels": LABELS,
            "comparisonMeaning": "Algorithm difference relative to Native TAA; no ground-truth accuracy or quality ranking.",
            "settings": {key: baseline[key] for key in COMMON}, "frames": [],
            "packages": {"Pillow": importlib.metadata.version("Pillow")}, "flip": None}
    for field in ("colorSpace", "display", "ui"):
        if field in baseline:
            data["settings"][field] = baseline[field]
    for index in range(baseline["frameCount"]):
        record = {"time": baseline["frames"][index]["timeSeconds"],
                  "simulationFrame": baseline["frames"][index]["simulationFrame"],
                  "images": {}, "hashes": {}, "differences": {}}
        for mode in MODES:
            source = root / mode / manifests[mode]["frames"][index]["file"]
            png = source if source.suffix.lower() == ".png" else source.with_suffix(".png")
            with Image.open(source) as img:
                if img.size != (baseline["width"], baseline["height"]):
                    raise ValueError(f"{source}: image extent differs from manifest")
                img.load()
                if png != source:
                    img.convert("RGB").save(png)
            record["images"][mode] = png.relative_to(root).as_posix()
            record["hashes"][mode] = {source.suffix.lower()[1:]: sha256(source), "png": sha256(png)}
        if with_flip:
            for mode in ("raw", "metalfx"):
                output = root / mode / f"difference-{index:06}.png"
                stats = flip_difference(root / record["images"]["taa"], root / record["images"][mode], output, ppd)
                stats["map"] = output.relative_to(root).as_posix()
                record["differences"][mode] = stats
        data["frames"].append(record)
    if with_flip:
        data["packages"].update({name: importlib.metadata.version(name) for name in ("flip-evaluator", "numpy")})
        data["flip"] = {"version": "1.7", "dynamicRange": "LDR", "inputColorSpace": "sRGB", "ppd": ppd,
                        "map": "grayscale 0..1, black = equal; fixed scale on all frames",
                        "mean": "Arithmetic mean of per-pixel LDR-FLIP values over the full frame",
                        "p95": "95th percentile of full-frame per-pixel values; NumPy linear interpolation",
                        "baseline": "Native TAA / TAAU at matching time and render scale, not ground truth"}
    (root / "comparison.json").write_text(json.dumps(data, indent=2, allow_nan=False) + "\n")
    template = Path(__file__).with_name("viewer.html").read_text()
    embedded = json.dumps(data, allow_nan=False).replace("<", "\\u003c").replace("&", "\\u0026")
    report = root / "index.html"
    report.write_text(template.replace("/* CAPTURE_DATA */", embedded))
    return report


def run_captures(args: argparse.Namespace) -> None:
    """Launch three identical schedules, preserving stdout and a hashed executable record."""
    root = args.output.resolve()
    if root.exists() and any(root.iterdir()):
        raise ValueError("capture output must be new or empty; use --report-only to inspect an existing run")
    root.mkdir(parents=True, exist_ok=True)
    app = args.app.resolve()
    if not app.is_file():
        raise ValueError(f"app binary missing: {app}")
    commands = []
    provenance = {}
    asset_root = Path(__file__).resolve().parents[2] / "Assets/Fetched/SanMiguel"
    if args.scene == "san-miguel":
        for name in ("PROVENANCE.json", "ARCHIVE_INFO.js", "LICENSE.txt"):
            source = asset_root / name
            if not source.is_file():
                raise ValueError(f"San Miguel provenance missing: {source}; run xmake setup --san-miguel")
            provenance[name] = {"sha256": sha256(source), "text": source.read_text()}
    executable_hash = sha256(app)
    shader_hashes = {p.name: sha256(p) for p in sorted((app.parent / "Shaders").glob("*")) if p.is_file()}
    if not shader_hashes:
        raise ValueError("no runtime shaders found beside App")
    for mode in MODES:
        command = [str(app), "--scene", args.scene, "--capture-sequence", str(root / mode),
                   "--frames", str(args.frames), "--warmup", str(args.warmup),
                   "--temporal", mode, "--render-scale", str(args.scale)]
        if args.capture_format is not None:
            command.extend(["--capture-format", args.capture_format])
        commands.append(command)
        environment = os.environ.copy()
        environment.pop("LMX_SCREENSHOT_NO_BLOOM", None)
        environment["MTL_DEBUG_LAYER"] = "1"
        with (root / f"{mode}.log").open("w") as log:
            result = subprocess.run(command, cwd=app.parent, env=environment, stdout=log, stderr=subprocess.STDOUT)
        (root / "run.json").write_text(json.dumps({"app": str(app), "appSha256": executable_hash,
            "workingDirectory": str(app.parent), "shaderSha256": shader_hashes, "sceneProvenance": provenance, "commands": commands,
            "environment": {"MTL_DEBUG_LAYER": "1", "LMX_SCREENSHOT_NO_BLOOM": "unset"}}, indent=2) + "\n")
        if result.returncode:
            raise ValueError(f"{mode} capture failed ({result.returncode}); see {root / (mode + '.log')}")
        current_shaders = {p.name: sha256(p) for p in sorted((app.parent / "Shaders").glob("*")) if p.is_file()}
        if sha256(app) != executable_hash or current_shaders != shader_hashes:
            raise ValueError("app binary or shaders changed during capture; comparison refused")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scene", default="san-miguel")
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--warmup", type=int, default=32)
    parser.add_argument("--scale", type=float, default=0.5)
    parser.add_argument("--capture-format", choices=("png", "bmp"),
                        help="Override App's sequence format (current default: png)")
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--flip", action="store_true", help="Add optional LDR-FLIP algorithm differences against Native TAA")
    parser.add_argument("--ppd", type=float, default=67.0)
    args = parser.parse_args()
    if args.frames < 1 or args.warmup < 0 or not 0.5 <= args.scale <= 1 or not math.isfinite(args.ppd) or args.ppd <= 0:
        parser.error("frames >= 1, warmup >= 0, scale in [0.5,1], and finite ppd > 0 are required")
    if not args.report_only and args.app is None:
        parser.error("--app is required unless --report-only is used")
    try:
        if not args.report_only:
            run_captures(args)
        print(build_report(args.output.resolve(), args.flip, args.ppd))
    except (ValueError, OSError, KeyError, ImportError) as error:
        print(f"comparison refused: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
