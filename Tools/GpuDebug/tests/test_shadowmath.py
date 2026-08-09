"""Tests for shadowmath.py -- the pure-Python mirror of Renderer.cpp's fitShadowOrtho.

These are the same four properties Tests/RenderTests.cpp asserts about the C++ original, restated
against the Python port. That duplication is the whole point: the port exists so a dump can say
"the shadowTransform you uploaded is not the one your scene state implies", and a port that drifts
from the C++ would turn that check into a false alarm generator. If a case here ever disagrees
with its RenderTests.cpp counterpart, the port is wrong -- Renderer.cpp is ground truth.

Where a property needs a known light-space basis (which axis is "right"), these tests pick a light
direction whose basis is derivable by hand from lookAtRH's own definition rather than asking
shadowmath for it -- an assertion that re-derives the implementation cannot catch the
implementation being wrong. The sphere-fit property, which must hold for *any* basis, is checked
the way RenderTests.cpp checks it: by sampling the sphere's surface and bounding the clip-space
extremes, so nothing about the basis leaks into the assertion.

Run: python3 -m unittest discover -s Tools/GpuDebug/tests -v
"""
import math
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

import shadowmath  # noqa: E402

# Tests/RenderTests.cpp's riggedLightDir(): the baseline scene's light 0, normalised.
_RIGGED_DIR = (0.577, -0.577, 0.577)


def _sphere_clip_extent(view_proj, sphere):
    """max|x|, max|y|, min z, max z over the sphere's surface, sampled -- RenderTests.cpp's
    sphereClipExtent, restated. Sampling rather than re-deriving keeps the assertion independent
    of how the fit chose its light-space basis, which no caller can see anyway."""
    cx, cy, cz, radius = sphere
    max_abs_x = max_abs_y = 0.0
    min_z, max_z = math.inf, -math.inf
    rings, segments = 32, 64
    for i in range(rings + 1):
        theta = math.pi * i / rings
        for j in range(segments):
            phi = 2.0 * math.pi * j / segments
            point = (cx + radius * math.sin(theta) * math.cos(phi),
                     cy + radius * math.cos(theta),
                     cz + radius * math.sin(theta) * math.sin(phi))
            clip = shadowmath.transform_point(view_proj, point)
            max_abs_x = max(max_abs_x, abs(clip[0]))
            max_abs_y = max(max_abs_y, abs(clip[1]))
            min_z, max_z = min(min_z, clip[2]), max(max_z, clip[2])
    return max_abs_x, max_abs_y, min_z, max_z


class FitShadowOrthoTests(unittest.TestCase):
    def test_sphere_centre_lands_mid_frustum_on_the_view_axis(self):
        """Property (a): the light looks *at* the centre, and the centre sits halfway through
        depth -- with the eye 2r out and the frustum running near = r to far = 3r, orthoRH_ZO maps
        z_view = -2r to exactly 0.5."""
        sphere = (0.0, 0.0, 0.0, 42.43)
        view_proj, _ = shadowmath.fit_shadow_ortho(sphere, _RIGGED_DIR)

        clip = shadowmath.transform_point(view_proj, sphere[:3])
        self.assertAlmostEqual(clip[3], 1.0, places=5)  # orthographic: no perspective divide
        self.assertAlmostEqual(clip[0], 0.0, places=4)
        self.assertAlmostEqual(clip[1], 0.0, places=4)
        self.assertAlmostEqual(clip[2], 0.5, places=4)
        self.assertTrue(0.0 <= clip[2] <= 1.0)

    def test_fits_the_bounding_sphere_exactly_off_origin_too(self):
        """Property (b), basis-independent half: the silhouette touches all four sides (exactly 1,
        not "at most 1" -- a loose fit wastes shadow-map texels, a tight-but-clipping one loses
        geometry) and spans the whole [0,1] Metal depth range. Off-origin on every axis, because a
        fit that quietly assumed a centred sphere passes at the origin and fails here."""
        sphere = (3.0, -1.0, 2.0, 7.0)
        view_proj, _ = shadowmath.fit_shadow_ortho(sphere, _RIGGED_DIR)

        max_abs_x, max_abs_y, min_z, max_z = _sphere_clip_extent(view_proj, sphere)
        self.assertAlmostEqual(max_abs_x, 1.0, places=3)
        self.assertAlmostEqual(max_abs_y, 1.0, places=3)
        self.assertAlmostEqual(min_z, 0.0, places=3)
        self.assertAlmostEqual(max_z, 1.0, places=3)

    def test_centre_plus_r_times_right_maps_to_clip_x_plus_one(self):
        """Property (b), basis-known half. For lightDir = (0,0,-1)
        the basis is hand-derivable from lookAtRH: f = (0,0,-1), s = normalize(cross(f, +Y)) =
        (+1,0,0), u = cross(s,f) = (0,+1,0). So +r along world +X is the fit's right edge."""
        sphere = (3.0, -1.0, 2.0, 7.0)
        centre, radius = sphere[:3], sphere[3]
        view_proj, _ = shadowmath.fit_shadow_ortho(sphere, (0.0, 0.0, -1.0))

        right_edge = (centre[0] + radius, centre[1], centre[2])
        left_edge = (centre[0] - radius, centre[1], centre[2])
        top_edge = (centre[0], centre[1] + radius, centre[2])
        self.assertAlmostEqual(shadowmath.transform_point(view_proj, right_edge)[0], 1.0, places=5)
        self.assertAlmostEqual(shadowmath.transform_point(view_proj, left_edge)[0], -1.0, places=5)
        self.assertAlmostEqual(shadowmath.transform_point(view_proj, top_edge)[1], 1.0, places=5)

    def test_light_pointing_straight_down_stays_finite_and_still_fits(self):
        """Property (c): lumine's literal launch state and one editor drag away -- a direction
        parallel to world up, where lookAt's cross product degenerates into a zero-length axis.
        The guard has to pick another up (+Z) rather than emit NaNs; a NaN matrix blanks the whole
        shadow map, which is exactly the failure this tool exists to name."""
        sphere = (0.0, 5.0, 0.0, 10.0)
        view_proj, shadow_transform = shadowmath.fit_shadow_ortho(sphere, (0.0, -1.0, 0.0))

        for matrix in (view_proj, shadow_transform):
            for column in matrix:
                for value in column:
                    self.assertTrue(math.isfinite(value), msg=f"{matrix} has a non-finite entry")

        max_abs_x, max_abs_y, min_z, max_z = _sphere_clip_extent(view_proj, sphere)
        self.assertAlmostEqual(max_abs_x, 1.0, places=3)
        self.assertAlmostEqual(max_abs_y, 1.0, places=3)
        self.assertAlmostEqual(min_z, 0.0, places=3)
        self.assertAlmostEqual(max_z, 1.0, places=3)

    def test_shadow_transform_bakes_the_ndc_to_texcoord_map(self):
        """Property (d): clip-space left/top is the shadow map's (0,0) texel and right/bottom is
        (1,1) -- x [-1,1] -> u [0,1], y [-1,1] -> v [1,0] because texture v runs down. Same
        hand-derivable basis as the right-edge case above: (-r,+r) in light space is world
        (-X,+Y) from the centre."""
        sphere = (0.0, 0.0, 0.0, 10.0)
        _, shadow_transform = shadowmath.fit_shadow_ortho(sphere, (0.0, 0.0, -1.0))

        left_top = (-10.0, 10.0, 0.0)
        right_bottom = (10.0, -10.0, 0.0)
        uv_left_top = shadowmath.transform_point(shadow_transform, left_top)
        uv_right_bottom = shadowmath.transform_point(shadow_transform, right_bottom)
        self.assertAlmostEqual(uv_left_top[0], 0.0, places=5)
        self.assertAlmostEqual(uv_left_top[1], 0.0, places=5)
        self.assertAlmostEqual(uv_right_bottom[0], 1.0, places=5)
        self.assertAlmostEqual(uv_right_bottom[1], 1.0, places=5)

    def test_shadow_transform_is_the_texcoord_remap_of_view_proj_everywhere(self):
        """The same bake stated as algebra on interior points, with a light whose basis is not
        axis-aligned: a wrong scale and a wrong offset cannot cancel each other out across three
        different points. z is untouched because Metal clip depth is already the [0,1] a D32Float
        shadow map stores."""
        sphere = (0.0, 0.0, 0.0, 10.0)
        view_proj, shadow_transform = shadowmath.fit_shadow_ortho(
            sphere, shadowmath.normalize((0.0, -1.0, 0.2)))

        for point in ((0.0, 0.0, 0.0), (4.0, -3.0, 6.0), (-7.0, 2.0, -1.0)):
            clip = shadowmath.transform_point(view_proj, point)
            shadow = shadowmath.transform_point(shadow_transform, point)
            self.assertAlmostEqual(shadow[0], 0.5 * clip[0] + 0.5, places=5)
            self.assertAlmostEqual(shadow[1], -0.5 * clip[1] + 0.5, places=5)
            self.assertAlmostEqual(shadow[2], clip[2], places=5)
            self.assertAlmostEqual(shadow[3], clip[3], places=5)

    def test_rejects_a_non_positive_radius(self):
        with self.assertRaises(ValueError):
            shadowmath.fit_shadow_ortho((0.0, 0.0, 0.0, 0.0), _RIGGED_DIR)

    def test_rejects_a_zero_light_direction(self):
        with self.assertRaises(ValueError):
            shadowmath.fit_shadow_ortho((0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0))


class MatrixHelperTests(unittest.TestCase):
    def test_multiply_is_column_major_like_glm(self):
        """m[col][row], glm's storage order. Multiplying a translation by a scale has to land the
        translation in column 3 -- the arrangement that would silently transpose is the one that
        puts it in row 3, and every downstream comparison against a captured upload depends on
        this being the same convention Renderer.cpp uses."""
        scale = [[2.0, 0, 0, 0], [0, 3.0, 0, 0], [0, 0, 4.0, 0], [0, 0, 0, 1.0]]
        translate = [[1.0, 0, 0, 0], [0, 1.0, 0, 0], [0, 0, 1.0, 0], [5.0, 6.0, 7.0, 1.0]]

        product = shadowmath.multiply(scale, translate)

        self.assertEqual(product[3], [10.0, 18.0, 28.0, 1.0])
        self.assertEqual(shadowmath.transform_point(product, (1.0, 1.0, 1.0)),
                         [12.0, 21.0, 32.0, 1.0])

    def test_determinant_of_identity_and_of_a_singular_matrix(self):
        self.assertAlmostEqual(shadowmath.determinant(shadowmath.identity()), 1.0, places=9)
        singular = [[1.0, 2, 3, 4], [2.0, 4, 6, 8], [0, 0, 1.0, 0], [0, 0, 0, 1.0]]
        self.assertAlmostEqual(shadowmath.determinant(singular), 0.0, places=9)

    def test_determinant_matches_the_product_of_ortho_scales(self):
        """An orthographic projection's determinant is the product of its diagonal, which is what
        anomalylib's degenerate-matrix check leans on: a fit that collapsed an axis shows up here
        as a determinant at zero, not as a plausible-looking matrix."""
        ortho = shadowmath.ortho_rh_zo(-2.0, 2.0, -4.0, 4.0, 1.0, 5.0)
        self.assertAlmostEqual(shadowmath.determinant(ortho), (2 / 4) * (2 / 8) * (-1 / 4),
                               places=9)


if __name__ == "__main__":
    unittest.main()
