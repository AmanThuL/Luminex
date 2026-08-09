"""A pure-Python mirror of `Renderer.cpp`'s `fitShadowOrtho` (and the two glm functions it calls),
so a dump can recompute the shadow matrices a captured frame *should* have uploaded and compare
them against the ones it actually did.

Ground truth is `lmx::render::fitShadowOrtho`, ported literally -- including the
degenerate-up guard and the texcoord bake, with the comments that carry the *why* brought across.
The properties `Tests/RenderTests.cpp` asserts about the original are re-asserted about this port
in `tests/test_shadowmath.py`; if the two ever disagree, this file is the one that is wrong.

**Storage convention: column-major, `m[col][row]`** -- glm's, and therefore the convention every
formula below is transcribed in. A matrix is a list of four columns, each a list of four floats,
so `m[3]` is the translation column. This is deliberately *not* the row-major shape
`uniformlib._decode_field` produces for display (`rows[row][col]`): a caller comparing a recompute
here against a decoded upload must transpose exactly one of the two, and `anomalylib` does.

Arithmetic is in Python doubles while the engine's is float32. That difference is why the
cross-check in `anomalylib` uses a relative tolerance rather than exact equality -- it is comparing
two honest answers computed at different precisions, not looking for bit-identity.

Run: python3 -c "import shadowmath"  (see tests/test_shadowmath.py for worked examples)
"""
import math

# Renderer.cpp's `kWorldUp`, and the dot-product threshold past which lookAt's cross product is
# too close to degenerate to trust.
_WORLD_UP = (0.0, 1.0, 0.0)
_DEGENERATE_UP_DOT = 0.999
_FALLBACK_UP = (0.0, 0.0, 1.0)


def identity() -> list:
    return [[1.0, 0.0, 0.0, 0.0],
            [0.0, 1.0, 0.0, 0.0],
            [0.0, 0.0, 1.0, 0.0],
            [0.0, 0.0, 0.0, 1.0]]


def normalize(v) -> tuple:
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if length == 0.0:
        raise ValueError("cannot normalize a zero-length vector")
    return (v[0] / length, v[1] / length, v[2] / length)


def cross(a, b) -> tuple:
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def dot(a, b) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def multiply(a, b) -> list:
    """`a * b` in glm's column-major storage: `(a*b)[col][row] = sum_k a[k][row] * b[col][k]`."""
    return [[sum(a[k][row] * b[col][k] for k in range(4)) for row in range(4)]
            for col in range(4)]


def transform_point(m, point) -> list:
    """`m * vec4(point, 1)` -> the 4-component result, undivided (these matrices are all
    orthographic, so w is always 1 and there is no perspective divide to do)."""
    v = (point[0], point[1], point[2], 1.0)
    return [sum(m[col][row] * v[col] for col in range(4)) for row in range(4)]


def determinant(m) -> float:
    """4x4 determinant by cofactor expansion along the first row.

    Storage order does not matter here -- `det(M) == det(M^T)` -- so this is correct whether it is
    handed a column-major matrix from this module or a row-major one decoded from a capture, which
    is exactly why `anomalylib`'s degenerate-matrix check can apply it to either without a
    transpose first.
    """
    def minor3(rows, cols):
        a, b, c = ((m[cols[0]][rows[0]], m[cols[1]][rows[0]], m[cols[2]][rows[0]]),
                   (m[cols[0]][rows[1]], m[cols[1]][rows[1]], m[cols[2]][rows[1]]),
                   (m[cols[0]][rows[2]], m[cols[1]][rows[2]], m[cols[2]][rows[2]]))
        return (a[0] * (b[1] * c[2] - b[2] * c[1])
                - a[1] * (b[0] * c[2] - b[2] * c[0])
                + a[2] * (b[0] * c[1] - b[1] * c[0]))

    total = 0.0
    for col in range(4):
        remaining = [c for c in range(4) if c != col]
        sign = -1.0 if col % 2 else 1.0
        total += sign * m[col][0] * minor3((1, 2, 3), remaining)
    return total


def look_at_rh(eye, center, up) -> list:
    """glm::lookAtRH, transcribed. Right-handed: the view axis is -z, which is the single place
    this whole file differs from lumine's left-handed original."""
    f = normalize((center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]))
    s = normalize(cross(f, up))
    u = cross(s, f)

    m = identity()
    m[0][0], m[1][0], m[2][0] = s[0], s[1], s[2]
    m[0][1], m[1][1], m[2][1] = u[0], u[1], u[2]
    m[0][2], m[1][2], m[2][2] = -f[0], -f[1], -f[2]
    m[3][0] = -dot(s, eye)
    m[3][1] = -dot(u, eye)
    m[3][2] = dot(f, eye)
    return m


def ortho_rh_zo(left, right, bottom, top, z_near, z_far) -> list:
    """glm::orthoRH_ZO, transcribed. `_ZO` is the [0,1] clip-depth convention -- Metal's, and the
    range a D32Float shadow map stores, which is what lets CalcShadowFactor compare a sampled depth
    against a transformed one with no remap in between."""
    m = identity()
    m[0][0] = 2.0 / (right - left)
    m[1][1] = 2.0 / (top - bottom)
    m[2][2] = -1.0 / (z_far - z_near)
    m[3][0] = -(right + left) / (right - left)
    m[3][1] = -(top + bottom) / (top - bottom)
    m[3][2] = -z_near / (z_far - z_near)
    return m


def fit_shadow_ortho(bounding_sphere, light_dir):
    """`(view_proj, shadow_transform)` for a directional light, both column-major `m[col][row]`.

    `bounding_sphere` is `(x, y, z, radius)` and `light_dir` is `(x, y, z)` -- the same
    `SceneView.boundingSphere` / `lights[0].direction` the capture sidecar records, so a caller can
    feed the sidecar's context straight in.

    Raises `ValueError` where the C++ asserts: a non-positive radius or a zero-length direction.
    Those are `LMX_ASSERT`s in `Renderer.cpp` (misuse, not a runtime condition), but a dump reads
    an untrusted sidecar rather than a live SceneView, so here they are recoverable errors -- the
    caller degrades the cross-check to "cannot recompute" instead of the tool crashing.
    """
    center = (bounding_sphere[0], bounding_sphere[1], bounding_sphere[2])
    radius = bounding_sphere[3]
    if not radius > 0.0:
        raise ValueError(f"fit_shadow_ortho: the bounding sphere's radius must be positive, got "
                         f"{radius!r}")
    if not (light_dir[0] or light_dir[1] or light_dir[2]):
        raise ValueError("fit_shadow_ortho: the light direction must not be the zero vector")

    direction = normalize(light_dir)
    # Offset from the sphere center so translated scenes retain the same fitted light volume.
    eye = (center[0] - 2.0 * radius * direction[0],
           center[1] - 2.0 * radius * direction[1],
           center[2] - 2.0 * radius * direction[2])

    # Avoid lookAt's degenerate cross product when the light is parallel to world up.
    up = _FALLBACK_UP if abs(dot(direction, _WORLD_UP)) > _DEGENERATE_UP_DOT else _WORLD_UP
    light_view = look_at_rh(eye, center, up)

    # By construction this is (0, 0, -2r); it is computed rather than assumed because that is
    # lumine's shape and because it states the fit instead of restating the eye placement.
    center_ls = transform_point(light_view, center)
    # Right-handed view space looks down -z, so the distance to a point along the view axis is -z.
    light_proj = ortho_rh_zo(center_ls[0] - radius, center_ls[0] + radius,
                             center_ls[1] - radius, center_ls[1] + radius,
                             -center_ls[2] - radius, -center_ls[2] + radius)

    # lumine's T: NDC x [-1,1] -> u [0,1], y [-1,1] -> v [1,0] (texture v runs down), and nothing
    # for z -- Metal's clip depth is already the [0,1] a D32Float shadow map stores.
    ndc_to_texcoord = identity()
    ndc_to_texcoord[0][0] = 0.5
    ndc_to_texcoord[1][1] = -0.5
    ndc_to_texcoord[3][0] = 0.5
    ndc_to_texcoord[3][1] = 0.5

    view_proj = multiply(light_proj, light_view)
    return view_proj, multiply(ndc_to_texcoord, view_proj)
