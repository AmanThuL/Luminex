#!/usr/bin/env python3
"""Bake every base-color/normal image a glTF or GLB file references into a sibling Baked/*.dds.

Usage: bake_gltf_textures.py <gltf-or-glb-path> <TextureBake-binary>

Reads the file's own "materials"/"textures"/"images" arrays (no external glTF library) to find
which image indices are bound as a baseColorTexture (--srgb) or a normalTexture (--normal-map) --
the only two slots Engine/Scene.cpp's ensureUploaded consumes today. Every other image (occlusion,
metallic-roughness, emissive, ...) is left unbaked; it has no reader yet. Each selected image is
handed to the TextureBake binary, which writes "<gltfDir>/Baked/image<N>.dds" plus its manifest --
"image<N>" rather than the source filename because a GLB's images are embedded with no filename at
all, and Engine/Scene.cpp's bakedDdsPath looks up by the same glTF/GLB image-array index this
script uses. Re-running with unchanged inputs is a no-op: a bake is skipped whenever the existing
manifest's source hash, role-specific filter, and tool version all match.
"""

from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

GLB_MAGIC = 0x46546C67
GLB_CHUNK_JSON = 0x4E4F534A
GLB_CHUNK_BIN = 0x004E4942

MIME_EXTENSION = {
    "image/png": ".png",
    "image/jpeg": ".jpg",
}

# Must match Tools/TextureBake/main.cpp. Both the algorithm revision and the role-specific filter
# are part of a baked artifact's identity; a matching source hash alone does not make an old bake
# current after either one changes.
TEXTURE_BAKE_TOOL_VERSION = "1"
ROLE_FILTER = {
    "srgb": "box-linear",
    "normal-map": "box-normal",
}


def read_glb(data: bytes) -> tuple[dict, bytes | None]:
    """Returns (json chunk decoded, binary chunk bytes or None)."""
    magic, _version, length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC:
        raise ValueError("not a GLB file (bad magic)")
    offset = 12
    json_bytes: bytes | None = None
    bin_bytes: bytes | None = None
    while offset < length:
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        chunk_start = offset + 8
        chunk_data = data[chunk_start : chunk_start + chunk_length]
        if chunk_type == GLB_CHUNK_JSON:
            json_bytes = chunk_data
        elif chunk_type == GLB_CHUNK_BIN:
            bin_bytes = chunk_data
        offset = chunk_start + chunk_length
    if json_bytes is None:
        raise ValueError("GLB file has no JSON chunk")
    return json.loads(json_bytes), bin_bytes


def image_roles(gltf: dict) -> dict[int, str]:
    """Maps image index -> "srgb" or "normal-map" from every used material's texture bindings."""
    textures = gltf.get("textures", [])
    roles: dict[int, str] = {}

    def source_of(texture_index: int) -> int | None:
        if texture_index < 0 or texture_index >= len(textures):
            return None
        return textures[texture_index].get("source")

    for material in gltf.get("materials", []):
        pbr = material.get("pbrMetallicRoughness", {})
        base_color = pbr.get("baseColorTexture")
        if base_color is not None:
            source = source_of(base_color["index"])
            if source is not None:
                roles[source] = "srgb"
        normal = material.get("normalTexture")
        if normal is not None:
            source = source_of(normal["index"])
            if source is not None:
                roles[source] = "normal-map"
    return roles


def encoded_image_bytes(gltf: dict, image_index: int, gltf_dir: Path,
                        bin_chunk: bytes | None) -> tuple[bytes, str]:
    """Returns (encoded file bytes, a human-meaningful source name for the manifest)."""
    image = gltf["images"][image_index]
    if "uri" in image:
        uri = image["uri"]
        if uri.startswith("data:"):
            raise ValueError(f"image {image_index}: data URIs are not supported")
        path = gltf_dir / uri
        return path.read_bytes(), uri
    if "bufferView" in image:
        buffer_view = gltf["bufferViews"][image["bufferView"]]
        buffer_index = buffer_view.get("buffer", 0)
        buffer = gltf.get("buffers", [{}])[buffer_index]
        offset = buffer_view.get("byteOffset", 0)
        length = buffer_view["byteLength"]
        if "uri" not in buffer:
            if bin_chunk is None:
                raise ValueError(f"image {image_index}: buffer {buffer_index} has no uri and "
                                 "there is no GLB binary chunk")
            source_bytes = bin_chunk[offset : offset + length]
        else:
            source_bytes = (gltf_dir / buffer["uri"]).read_bytes()[offset : offset + length]
        mime = image.get("mimeType", "")
        name = f"image{image_index}{MIME_EXTENSION.get(mime, '')}"
        return source_bytes, name
    raise ValueError(f"image {image_index} has neither uri nor bufferView")


def manifest_is_current(manifest_path: Path, dds_path: Path, source_sha256: str,
                        expected_filter: str,
                        expected_tool_version: str = TEXTURE_BAKE_TOOL_VERSION) -> bool:
    if not dds_path.is_file() or not manifest_path.is_file():
        return False
    try:
        manifest = json.loads(manifest_path.read_text())
    except (OSError, json.JSONDecodeError):
        return False
    return (
        manifest.get("sourceSha256") == source_sha256
        and manifest.get("filter") == expected_filter
        and manifest.get("toolVersion") == expected_tool_version
    )


def texture_bake_args(texture_bake: str, gltf_path: Path, image: dict, source_name: str,
                      mode_flag: str, dds_path: Path, in_path: Path) -> list[str]:
    """Builds the TextureBake argv for one image. `--source-name` is always explicit and always a
    relative, machine-independent string (a glTF-relative URI, or "<glbName>#image<N>.<ext>") --
    never `in_path` itself, which for an external image is the real (and possibly absolute)
    filesystem path TextureBake decodes from. Manifests must be byte-identical across machines, so
    the recorded "source" field must never depend on where the repository happens to be checked
    out.
    """
    if "uri" in image:
        return [texture_bake, str(in_path), str(dds_path), mode_flag, "--source-name", source_name]
    return [texture_bake, str(in_path), str(dds_path), mode_flag, "--source-name",
           f"{gltf_path.name}#{source_name}"]


def bake_gltf_textures(gltf_path: Path, texture_bake: str) -> tuple[int, int]:
    """Bakes every base-color/normal image `gltf_path` references. Returns (baked, skipped)."""
    gltf_path = gltf_path.resolve()
    data = gltf_path.read_bytes()
    bin_chunk: bytes | None = None
    if gltf_path.suffix.lower() == ".glb":
        gltf, bin_chunk = read_glb(data)
    else:
        gltf = json.loads(data)

    roles = image_roles(gltf)
    baked_dir = gltf_path.parent / "Baked"
    baked_dir.mkdir(exist_ok=True)

    baked_count = 0
    skipped_count = 0
    for image_index, role in sorted(roles.items()):
        image = gltf["images"][image_index]
        source_bytes, source_name = encoded_image_bytes(gltf, image_index, gltf_path.parent,
                                                         bin_chunk)
        source_sha256 = hashlib.sha256(source_bytes).hexdigest()
        dds_path = baked_dir / f"image{image_index}.dds"
        manifest_path = baked_dir / f"image{image_index}.dds.json"
        if manifest_is_current(manifest_path, dds_path, source_sha256, ROLE_FILTER[role]):
            skipped_count += 1
            continue

        mode_flag = "--srgb" if role == "srgb" else "--normal-map"
        if "uri" in image:
            # An external file: hand TextureBake the real path directly, no temp file needed.
            in_path = gltf_path.parent / image["uri"]
            args = texture_bake_args(texture_bake, gltf_path, image, source_name, mode_flag,
                                     dds_path, in_path)
            subprocess.run(args, check=True)
        else:
            suffix = Path(source_name).suffix or ".bin"
            with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as temp_file:
                temp_file.write(source_bytes)
                temp_path = Path(temp_file.name)
            try:
                args = texture_bake_args(texture_bake, gltf_path, image, source_name, mode_flag,
                                         dds_path, temp_path)
                subprocess.run(args, check=True)
            finally:
                temp_path.unlink(missing_ok=True)
        baked_count += 1

    return baked_count, skipped_count


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <gltf-or-glb-path> <TextureBake-binary>", file=sys.stderr)
        return 2
    gltf_path = Path(sys.argv[1])
    texture_bake = sys.argv[2]

    baked_count, skipped_count = bake_gltf_textures(gltf_path, texture_bake)
    print(f"bake_gltf_textures: {gltf_path.name}: {baked_count} baked, {skipped_count} "
         "already current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
