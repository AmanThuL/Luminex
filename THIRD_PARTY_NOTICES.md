# Third-party asset notices

The Apache-2.0 license in this repository covers Luminex source code. It does not cover files
downloaded into `Assets/Fetched/`, nor the gallery images derived from those files:
`docs/media/sponza.png`, `docs/media/editor-sponza.jpg` and `docs/media/damaged-helmet.png`.
Fetched third-party fonts retain their own licenses as listed below.

## Damaged Helmet

- Source: KhronosGroup/glTF-Sample-Assets, `Models/DamagedHelmet`
- Pinned commit: `2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf`
- Retrieved files: `glTF-Binary/DamagedHelmet.glb`, `LICENSE.md`, and `metadata.json`
- Rebuild and glTF conversion: ctxwing, 2018, CC BY 4.0
- Earlier version of the model: theblueturtle_, 2016, CC BY-NC 4.0

Both upstream license layers apply. The CC-BY-NC-4.0 layer restricts the model and derivatives to
non-commercial use. `xmake setup` installs the exact upstream license and metadata beside the GLB;
retain those files and the attribution when using the model or publishing derived images. The
Luminex gallery image changes the upstream work by rendering it with Luminex lighting, camera,
shading, and image output.

Upstream license record:
<https://github.com/KhronosGroup/glTF-Sample-Assets/tree/2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf/Models/DamagedHelmet>

License texts: [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/legalcode) and
[CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/legalcode).

## Sponza

- Source: Morgan McGuire's Computer Graphics Archive, Crytek Sponza
- Retrieved input: the official approximately 78 MB OBJ+PNG archive and its `info.js` record
- License recorded by `info.js`: [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/legalcode)
- Attribution: Crytek Sponza, © 2010 Frank Meinl/Crytek

`docs/media/editor-sponza.jpg` shows Sponza rendered in the real Luminex editor with native TAA.
Its camera framing, lighting and image output are adaptations. The scene, editor content and
native macOS window frame are captured from the running application.

`xmake setup` verifies the source archive and then performs a deterministic local conversion to
the uncompressed core glTF subset used by Luminex. The converted geometry, materials, and image
references are adaptations of the upstream work and remain under CC BY 3.0; the conversion does
not relicense them under Apache-2.0. Retain this attribution and a link to the license when
redistributing the converted scene or publishing derived images.

Archive record and download: <https://casual-effects.com/data>

Suggested archive citation: Morgan McGuire, *Computer Graphics Archive*, July 2017,
<https://casual-effects.com/data>.

## San Miguel (optional)

- Source: Morgan McGuire's Computer Graphics Archive, `San_Miguel/San_Miguel.zip`
- Chosen input: `san-miguel-low-poly.obj`, its material file and referenced PNGs
- Author: Guillermo M. Leal Llaguno; improvements credited to Morgan McGuire, Guedis Cardenas,
  Michael Mara and Nicholas Hull
- Archive `info.js`: San Miguel 2.0, CC BY 3.0
- Enclosed `license.txt`: San Miguel 2.1, with research/educational use and attribution wording

`xmake setup --san-miguel` preserves both upstream records verbatim as `ARCHIVE_INFO.js` and
`LICENSE.txt`; `PROVENANCE.json` records source URLs, hashes, the chosen variant and adaptations.
The metadata/version discrepancy is preserved, not resolved by the local conversion. Retain both
records and credits with the fetched asset and derived comparisons. The conversion imports metre
geometry, approximates Phong materials, uses diffuse alpha for cutouts and selected tangent-space
normal maps; Luminex's lighting, camera and rendering further change derived images.

Archive record: <https://casual-effects.com/data>.

## Inter

- Source: [Inter 4.1](https://github.com/rsms/inter/tree/e3a3d4c57d5ecc01453a575621882a384c1995a3)
- Author: Rasmus Andersson and contributors
- File: unmodified `docs/font-files/InterVariable.ttf`, using default Regular outlines
- License: SIL Open Font License 1.1, retained verbatim alongside the font

`xmake setup` verifies the pinned font and license hashes. Building App copies the font,
`LICENSE.txt` and `SOURCE.txt` to its `Fonts/` directory; keep them together when distributing App.
The editor sets digit advances at runtime without modifying the font file. Gallery screenshots
show the typeface in use; the font itself is not relicensed under Apache-2.0.
