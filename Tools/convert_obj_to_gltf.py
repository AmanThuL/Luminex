#!/usr/bin/env python3
"""Convert a McGuire OBJ package to core glTF, preserving the legacy Sponza defaults."""

from __future__ import annotations

import argparse
import json
import math
import shutil
import struct
from collections import OrderedDict
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Material:
    name: str
    diffuse: tuple[float, float, float] = (1.0, 1.0, 1.0)
    shininess: float = 0.0
    diffuse_map: str | None = None
    bump_map: str | None = None


@dataclass
class Primitive:
    material: Material
    vertices: list[tuple[int, int, int]] = field(default_factory=list)
    indices: list[int] = field(default_factory=list)
    vertex_map: dict[tuple[int, int, int], int] = field(default_factory=dict)

    def add_corner(self, corner: tuple[int, int, int]) -> None:
        index = self.vertex_map.get(corner)
        if index is None:
            index = len(self.vertices)
            self.vertex_map[corner] = index
            self.vertices.append(corner)
        self.indices.append(index)


def parse_mtl(path: Path) -> OrderedDict[str, Material]:
    materials: OrderedDict[str, Material] = OrderedDict()
    current: Material | None = None
    for raw_line in path.read_text(encoding="utf-8-sig").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        keyword, _, value = line.partition(" ")
        value = value.strip()
        if keyword == "newmtl":
            current = Material(value)
            materials[value] = current
        elif current is not None and keyword == "Kd":
            components = value.split()
            if len(components) != 3:
                raise ValueError(f"{path}: malformed Kd for {current.name}")
            current.diffuse = tuple(float(component) for component in components)
        elif current is not None and keyword == "Ns":
            current.shininess = float(value)
        elif current is not None and keyword == "map_Kd":
            current.diffuse_map = value.replace("\\", "/")
        elif current is not None and keyword.lower() in ("map_bump", "bump"):
            current.bump_map = value.replace("\\", "/")
    if not materials:
        raise ValueError(f"{path}: no materials")
    return materials


def resolve_obj_index(value: str, count: int, what: str, line_number: int) -> int:
    parsed = int(value)
    index = parsed - 1 if parsed > 0 else count + parsed
    if index < 0 or index >= count:
        raise ValueError(f"OBJ line {line_number}: {what} index {parsed} is out of range")
    return index


def parse_obj(
    path: Path, materials: OrderedDict[str, Material]
) -> tuple[
    list[tuple[float, float, float]],
    list[tuple[float, float]],
    list[tuple[float, float, float]],
    list[Primitive],
]:
    positions: list[tuple[float, float, float]] = []
    texcoords: list[tuple[float, float]] = []
    normals: list[tuple[float, float, float]] = []
    primitives = OrderedDict((name, Primitive(material)) for name, material in materials.items())
    current: Primitive | None = None

    with path.open(encoding="utf-8-sig") as source:
        for line_number, raw_line in enumerate(source, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            keyword, _, value = line.partition(" ")
            fields = value.split()
            if keyword == "v":
                if len(fields) < 3:
                    raise ValueError(f"OBJ line {line_number}: malformed position")
                positions.append(tuple(float(component) for component in fields[:3]))
            elif keyword == "vt":
                if len(fields) < 2:
                    raise ValueError(f"OBJ line {line_number}: malformed texture coordinate")
                texcoords.append((float(fields[0]), 1.0 - float(fields[1])))
            elif keyword == "vn":
                if len(fields) < 3:
                    raise ValueError(f"OBJ line {line_number}: malformed normal")
                normals.append(tuple(float(component) for component in fields[:3]))
            elif keyword == "usemtl":
                if value not in primitives:
                    raise ValueError(f"OBJ line {line_number}: unknown material {value!r}")
                current = primitives[value]
            elif keyword == "f":
                if current is None:
                    raise ValueError(f"OBJ line {line_number}: face has no material")
                if len(fields) < 3:
                    raise ValueError(f"OBJ line {line_number}: face has fewer than three corners")
                corners: list[tuple[int, int, int]] = []
                for field_value in fields:
                    parts = field_value.split("/")
                    if len(parts) != 3 or not all(parts):
                        raise ValueError(
                            f"OBJ line {line_number}: every corner must have position/UV/normal"
                        )
                    corners.append(
                        (
                            resolve_obj_index(parts[0], len(positions), "position", line_number),
                            resolve_obj_index(parts[1], len(texcoords), "UV", line_number),
                            resolve_obj_index(parts[2], len(normals), "normal", line_number),
                        )
                    )
                for corner_index in range(1, len(corners) - 1):
                    current.add_corner(corners[0])
                    current.add_corner(corners[corner_index])
                    current.add_corner(corners[corner_index + 1])

    populated = [primitive for primitive in primitives.values() if primitive.indices]
    if not populated:
        raise ValueError(f"{path}: no faces")
    return positions, texcoords, normals, populated


def align(buffer: bytearray, alignment: int = 4) -> None:
    buffer.extend(b"\0" * (-len(buffer) % alignment))


def append_view(
    gltf: dict, binary: bytearray, payload: bytes, target: int, byte_stride: int | None = None
) -> int:
    align(binary)
    offset = len(binary)
    binary.extend(payload)
    view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload), "target": target}
    if byte_stride is not None:
        view["byteStride"] = byte_stride
    gltf["bufferViews"].append(view)
    return len(gltf["bufferViews"]) - 1


def append_accessor(
    gltf: dict,
    view: int,
    component_type: int,
    count: int,
    accessor_type: str,
    minimum: list[float] | None = None,
    maximum: list[float] | None = None,
) -> int:
    accessor = {
        "bufferView": view,
        "byteOffset": 0,
        "componentType": component_type,
        "count": count,
        "type": accessor_type,
    }
    if minimum is not None:
        accessor["min"] = minimum
    if maximum is not None:
        accessor["max"] = maximum
    gltf["accessors"].append(accessor)
    return len(gltf["accessors"]) - 1


def pack_floats(values: list[tuple[float, ...]]) -> bytes:
    flattened = (component for value in values for component in value)
    return struct.pack(f"<{sum(len(value) for value in values)}f", *flattened)


def make_materials(
    source_root: Path, output_root: Path, materials: list[Material], gltf: dict,
    alpha_mask: bool = False,
    normal_map_prefix: str = "", phong_roughness: bool = False,
) -> dict[str, int]:
    texture_indices: dict[str, int] = {}
    material_indices: dict[str, int] = {}
    transparency: dict[str, bool] = {}
    for material in materials:
        pbr = {
            "baseColorFactor": [*material.diffuse, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 1.0 - min(max(material.shininess, 0.0), 256.0) / 256.0,
        }
        if phong_roughness:
            pbr["roughnessFactor"] = max(0.04, math.sqrt(2.0 / (max(0.0, material.shininess) + 2.0)))
        if material.diffuse_map is not None:
            relative = Path(material.diffuse_map)
            source = (source_root / relative).resolve()
            if source_root.resolve() not in source.parents or not source.is_file():
                raise ValueError(f"material {material.name}: missing texture {relative.as_posix()}")
            uri = relative.as_posix()
            texture_index = texture_indices.get(uri)
            if texture_index is None:
                destination = output_root / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
                gltf["images"].append({"uri": uri})
                gltf["textures"].append({"source": len(gltf["images"]) - 1})
                texture_index = len(gltf["textures"]) - 1
                texture_indices[uri] = texture_index
            pbr["baseColorTexture"] = {"index": texture_index}
        output_material = {"name": material.name, "pbrMetallicRoughness": pbr, "doubleSided": False}
        if alpha_mask and material.diffuse_map is not None:
            try:
                from .png_alpha import has_transparency
            except ImportError:
                from png_alpha import has_transparency
            if uri not in transparency:
                transparency[uri] = has_transparency(source)
            if transparency[uri]:
                output_material.update(alphaMode="MASK", alphaCutoff=0.5, doubleSided=True)
        # Only an explicitly selected archive naming convention promotes a bump entry to a
        # tangent-space normal map. Legacy Sponza height maps remain deliberately unconverted.
        if normal_map_prefix and material.bump_map and Path(material.bump_map).name.startswith(normal_map_prefix):
            relative = Path(material.bump_map)
            source = (source_root / relative).resolve()
            if source_root.resolve() not in source.parents or not source.is_file():
                raise ValueError(f"material {material.name}: missing normal texture {relative}")
            uri = relative.as_posix()
            if uri not in texture_indices:
                destination = output_root / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
                gltf["images"].append({"uri": uri})
                gltf["textures"].append({"source": len(gltf["images"]) - 1})
                texture_indices[uri] = len(gltf["textures"]) - 1
            output_material["normalTexture"] = {"index": texture_indices[uri]}
        gltf["materials"].append(output_material)
        material_indices[material.name] = len(gltf["materials"]) - 1
    return material_indices


def convert(obj: Path, mtl: Path, output_root: Path, scale: float,
            name: str = "Sponza", alpha_mask: bool = False,
            normal_map_prefix: str = "", phong_roughness: bool = False) -> None:
    if not math.isfinite(scale) or scale <= 0.0:
        raise ValueError("scale must be finite and greater than zero")
    if not name or Path(name).name != name or name in (".", ".."):
        raise ValueError("name must be a nonempty filename stem")
    materials = parse_mtl(mtl)
    positions, texcoords, normals, primitives = parse_obj(obj, materials)
    output_root.mkdir(parents=True, exist_ok=True)

    gltf = {
        "asset": {"version": "2.0", "generator": "Luminex convert_obj_to_gltf.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Crytek Sponza" if name == "Sponza" else name, "mesh": 0}],
        "meshes": [{"name": "Crytek Sponza" if name == "Sponza" else name, "primitives": []}],
        "materials": [],
        "textures": [],
        "images": [],
        "buffers": [],
        "bufferViews": [],
        "accessors": [],
    }
    used_materials = [primitive.material for primitive in primitives]
    material_indices = make_materials(obj.parent, output_root, used_materials, gltf, alpha_mask,
                                     normal_map_prefix, phong_roughness)
    binary = bytearray()

    for primitive in primitives:
        primitive_positions = [
            tuple(component * scale for component in positions[corner[0]])
            for corner in primitive.vertices
        ]
        primitive_texcoords = [texcoords[corner[1]] for corner in primitive.vertices]
        primitive_normals = [normals[corner[2]] for corner in primitive.vertices]
        minimum = [min(value[axis] for value in primitive_positions) for axis in range(3)]
        maximum = [max(value[axis] for value in primitive_positions) for axis in range(3)]

        position_view = append_view(gltf, binary, pack_floats(primitive_positions), 34962)
        normal_view = append_view(gltf, binary, pack_floats(primitive_normals), 34962)
        texcoord_view = append_view(gltf, binary, pack_floats(primitive_texcoords), 34962)
        index_payload = struct.pack(f"<{len(primitive.indices)}I", *primitive.indices)
        index_view = append_view(gltf, binary, index_payload, 34963)

        gltf["meshes"][0]["primitives"].append(
            {
                "attributes": {
                    "POSITION": append_accessor(
                        gltf, position_view, 5126, len(primitive_positions), "VEC3", minimum, maximum
                    ),
                    "NORMAL": append_accessor(
                        gltf, normal_view, 5126, len(primitive_normals), "VEC3"
                    ),
                    "TEXCOORD_0": append_accessor(
                        gltf, texcoord_view, 5126, len(primitive_texcoords), "VEC2"
                    ),
                },
                "indices": append_accessor(
                    gltf, index_view, 5125, len(primitive.indices), "SCALAR"
                ),
                "material": material_indices[primitive.material.name],
                "mode": 4,
            }
        )

    binary_name = name + ".bin"
    gltf["buffers"].append({"uri": binary_name, "byteLength": len(binary)})
    (output_root / binary_name).write_bytes(binary)
    (output_root / (name + ".gltf")).write_text(
        json.dumps(gltf, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("obj", type=Path)
    parser.add_argument("mtl", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--scale", type=float, default=0.01)
    parser.add_argument("--name", default="Sponza")
    parser.add_argument("--alpha-mask", action="store_true",
                        help="preserve diffuse PNG transparency as two-sided MASK materials")
    parser.add_argument("--normal-map-prefix", default="",
                        help="explicit archive filename prefix identifying tangent-space maps")
    parser.add_argument("--phong-roughness", action="store_true",
                        help="approximate Phong exponent with sqrt(2/(Ns+2)), floored at 0.04")
    args = parser.parse_args()
    convert(args.obj, args.mtl, args.output, args.scale, args.name, args.alpha_mask,
            args.normal_map_prefix, args.phong_roughness)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
