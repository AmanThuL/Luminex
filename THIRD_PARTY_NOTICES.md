# Third-party asset notices

The Apache-2.0 license in this repository covers Luminex source code. It does not cover files
downloaded into `Assets/Fetched/`, nor the gallery images derived from those files:
`docs/media/sponza.png` and `docs/media/damaged-helmet.png`.

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

`xmake setup` verifies the source archive and then performs a deterministic local conversion to
the uncompressed core glTF subset used by Luminex. The converted geometry, materials, and image
references are adaptations of the upstream work and remain under CC BY 3.0; the conversion does
not relicense them under Apache-2.0. Retain this attribution and a link to the license when
redistributing the converted scene or publishing derived images.

Archive record and download: <https://casual-effects.com/data>

Suggested archive citation: Morgan McGuire, *Computer Graphics Archive*, July 2017,
<https://casual-effects.com/data>.
