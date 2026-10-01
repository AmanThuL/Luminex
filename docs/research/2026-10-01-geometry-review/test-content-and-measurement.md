# Test Content and Measurement for Cluster Geometry

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
covering openly licensed dense test content and how Nanite-like systems are benchmarked. Licenses
and terms were read from fetched pages and archives; triangle counts marked *measured* come from
glTF accessor counts, PLY headers or STL headers of files downloaded for this task; sizes come from
HTTP `Content-Length` headers. Items marked **[UNVERIFIED]** were not confirmed against a primary
source; recheck them before a plan depends on them. **Reported** means a search-engine summary,
press article or a repository's own claim that I could not check against the original.

Credit: Nanite and the other Unreal Engine systems described here are the work of Epic Games.
Unreal Engine source is copyright Epic Games, Inc. and was read under the Unreal Engine EULA; this
notebook describes mechanisms in its own words and reproduces no engine code, shader text or
comments. Unreal, Unreal Engine and Nanite are trademarks of Epic Games, Inc.

## Findings in brief

- The only permissively licensed sources of very large single scans I found are the Smithsonian CC0
  full-resolution OBJ tiers (700 MB to 900 MB zips, 2.4 GB to 3.9 GB of OBJ text per object; triangle
  counts not measured) and a 6.0 GB Helsinki city mesh. The practical sweet spot for a solo,
  hash-pinned project is **Poly Haven CC0 photogrammetry**: measured glTF triangle counts of 0.4 M
  to 17.2 M for its rocks, cliffs and trees, with UVs, normals and PBR textures, as direct CDN files
  on the host Luminex already uses for its HDRI.
- Poly Haven's API `polycount` field is not the triangle count of the downloadable glTF (2.36 M
  advertised versus 1.26 M measured for `coast_rocks_02`; 0.31 M advertised versus 3.86 M measured
  for `jacaranda_tree`). Select by measured glTF counts.
- The Stanford models are the lingua franca of published numbers but carry research-only,
  non-commercial terms, and image publication is granted only for "a scholarly article or book".
  A public portfolio is not obviously covered. That is the main license risk in this survey.
- Several well-known dense scenes are license traps: Emerald Square and SpeedTree (CC BY-NC-SA),
  Power Plant (no commercial use, games named explicitly; origin page now 404), Intel Sponza (a
  "personal and educational use" header above CC BY 4.0 text), Moana (research or software
  development, including benchmarking, only; 15 GB to 48 GB), Khronos' copy of Sponza (CryEngine
  Limited License).
- The expectation that Megascans and Unreal content cannot leave Unreal does not hold for content
  acquired on Fab under the Fab Standard License: its binding text grants private use without
  naming an engine, the page's non-binding summary says usage "is not limited to Unreal Engine",
  and listings ship FBX and glTF. It still holds for content designated "UE-Only" under the separate
  Epic Content License Agreement, which includes Megascans acquired under an Unreal Engine plan.
  For a public fixture the real obstacles are purchase and login, no standalone redistribution (so
  no pinned public fetch), and a "NoAI" flag on listings.
- The Nanite-like benchmarks I read report the same few things: source triangles, per-stage
  survivor counts (instances, nodes, candidate clusters, visible clusters, rasterized triangles),
  per-pass GPU milliseconds, bytes per triangle, and for the offline side wall time and peak RAM.
  The headline result being tested is that rasterized work stays roughly constant as source
  complexity grows (Nanite: about 25 M rasterized triangles per frame against a billion-plus
  nominal), so a benchmark must sweep source complexity.
- On this machine (Apple M3 Max, macOS 26.7) the public Metal counter API exposes only a
  timestamp counter set and stage-boundary sampling. Every cluster count must therefore be written
  by the culling shaders themselves, as Luminex's visibility counters already are.

## 1. Content already in the repository, measured

| Content | Source | Measured triangles | Structure | License as recorded |
|---|---|---:|---|---|
| Crytek Sponza | McGuire archive copy | 262,267 | 1 mesh, 25 primitives, 25 materials | metadata: CC BY 3.0 |
| San Miguel (realtime variant, repo conversion) | McGuire archive | 5,617,451 | 1 mesh, 1 node, 281 primitives, 281 materials, `MASK` and `OPAQUE`, 5,861,789 vertices | metadata CC BY 3.0; enclosed text "free for research and educational use with attribution" |

Both counts come from the converted glTF files in the local fetched-assets tree. San Miguel's
instancing was flattened by its authors (its `LICENSE.txt` says so), so there is no instance
structure to exploit: any cluster LOD gain on it comes from LOD and culling of one large,
multi-material mesh, not from instance reuse. The archive's own `info.js` lists San Miguel's
triangle count as `undefined`, so 5.6 M is the only count on record.

**Pin stability evidence.** The pinned Sponza archive SHA-256 in `xmake/setup.lua` still matches the
live download today (`da005cbee0be...`), although the McGuire downloads I checked all report
`Last-Modified: 2026-04-06`. The pinned San Miguel size (535,519,642 bytes) also still matches.
So `Last-Modified` is not a change signal on that host, but content has not changed. Poly Haven's
CDN is the opposite case: `Last-Modified` on `dl.polyhaven.org` (Cloudflare in front of a Backblaze
B2 bucket, by its response headers) reflected the current day for a file whose bytes matched the
API-published MD5; the `x-bz-upload-timestamp` header carries the object's real upload time
(2023-07-26 for both rock `.bin` files). Pin by hash, ignore `Last-Modified`.

## 2. Candidate survey

Terms columns follow the three questions asked: (a) download at setup time from a public URL,
(b) convert and bake locally, (c) publish screenshots and measurement tables in a public repository
and portfolio. "Yes" means the fetched text permits it; "Risk" means the text is silent, ambiguous
or narrower than that use.

| Candidate | License as written | (a) fetch | (b) bake | (c) publish | Pinnable |
|---|---|---|---|---|---|
| Stanford 3D Scanning Repository | research use; free redistribution; images only "in a scholarly article or book"; no commercial use | Yes | Risk (research use) | Risk | Yes (files dated 1996 to 2009) |
| Smithsonian Open Access 3D | CC0 per object, with third-party, trademark and cultural-sensitivity carve-outs | Yes (direct file URLs) | Yes | Yes | Yes by object UUID plus hash |
| Poly Haven models | CC0 | Yes (direct CDN files) | Yes | Yes | Yes by hash |
| Three D Scans | no license text on the site; press quotes only | Yes technically | Unclear | Unclear | Yes by hash |
| Khronos glTF Sample Assets | per asset: CC BY 4.0, CC0, Stanford, CryEngine, 3DRT test-only | Yes | per asset | per asset | Yes (git) |
| McGuire: Bistro | CC BY 4.0 | Yes | Yes | Yes with attribution | Yes |
| McGuire: Rungholt, Hairball | CC BY 3.0 | Yes | Yes | Yes with attribution | Yes |
| McGuire: Power Plant | UNC: no commercial use, games named | Yes | Risk | Risk | Origin page 404 |
| NVIDIA ORCA: Emerald Square, SpeedTree | CC BY-NC-SA 3.0 | Yes (no login) | NC | NC | Entry URL stable, final URL signed |
| Intel Sponza and add-ons | page: CC Attribution; archive header: personal and educational use | Yes | Risk | Risk | ID stable, URL signed |
| Intel Jungle Ruins | CC BY 4.0 text, no extra header | Yes | Needs Blender and add-ons | Yes with attribution | ID stable, URL signed |
| Disney Moana Island | research or software development (including benchmarking) only; naming rules | Yes | Research only | Risk (research and benchmark output, with naming rules) | Yes, but 15 GB to 48 GB |
| Quixel Megascans, Epic samples on Fab | Fab Standard License | No (account, purchase) | Private use | Images yes (EULA section 4, subject to section 6) | No |
| Blender demo files | per file: CC0, CC BY, CC BY-SA | Yes | Needs Blender | per file | Yes (dated URLs) |
| Helsinki Kalasatama mesh (Zenodo) | CC BY 4.0 | Yes (DOI, MD5 in record) | Yes | Yes with attribution | Best in this survey |
| NVIDIA Zorah glTF | MIT (Reported) | Yes (7z) | Yes | Yes | 7.2 GB to 70 GB |

| Candidate | Triangles | Download | Format | UV / normals / materials | Good for |
|---|---:|---:|---|---|---|
| Stanford Happy Buddha | 1,087,716 | 14.5 MB | ASCII PLY in tar.gz | none | single dense scan |
| Stanford Dragon | 871,414 (file); page says 1,132,830 | 11.2 MB | ASCII PLY | none | single dense scan |
| Stanford Lucy | 28,055,742 | 322 MB | binary PLY in tar.gz | none | extreme single scan |
| Smithsonian Triceratops 150k tier | 149,998 | 14.4 MB | GLB | UV, normals, 1 PBR material | textured scan, too small |
| Smithsonian full-resolution tier | not measured | 700 MB to 901 MB | OBJ zip (2.4 GB to 3.9 GB OBJ) | OBJ + MTL with TIFF or PNG textures; OBJ attributes not inspected | extreme single scan |
| Poly Haven `coast_rocks_02` | 1,260,423 | 38.5 MB | glTF + bin + 3 JPEG | UV, normals, 1 PBR material | single dense scan, instanced field |
| Poly Haven `coastal_cliff_04` | 1,537,926 | 46.5 MB | glTF | UV, normals, 1 material | wall-like occluder, instanced field |
| Poly Haven `pine_tree_01` | 17,182,252 (three trees) | 958 MB | glTF | UV, normals, 6 materials | geometric foliage |
| Three D Scans Oceanus | 2,000,050 | 78 MB zip | binary STL | none | single dense scan (license gap) |
| McGuire Bistro | 3,858,088 (archive) | 1,440 MB | OBJ + MTL + PNG | UV, normals, 198 materials | many-material scene |
| McGuire Rungholt | 6,704,264 | 49.8 MB | OBJ | atlas texture | cheap high-count regular mesh |
| McGuire Hairball | 2,880,000 | 52.6 MB | OBJ | none | thin-strand failure case |
| Intel Sponza base | 3,747,018 (measured) | 3,988 MB | glTF, FBX, USD, 3ds Max | UV x2, normals, tangents, 28 materials | modern Sponza |
| Helsinki Kalasatama | 30 M polygons (author) | 6,039 MB | OBJ + 50 PNG | UV, textures | real photogrammetry city |

### 2.1 Stanford 3D Scanning Repository

Primary: `graphics.stanford.edu/data/3Dscanrep/` (fetched in full) and the files themselves.

The acknowledgment paragraph reads: "You are welcome to use the data and models for research
purposes. You are also welcome to mirror or redistribute them for free. Finally, you may publish
images made using these models, or the images on this web site, in a scholarly article or book - as
long as credit is given to the Stanford Computer Graphics Laboratory. However, such models or
images are not to be used for commercial purposes, nor should they appear in a product for sale
(with the exception of scholarly journals or books), without our permission." A second paragraph
asks that the Buddha, Dragon, Thai statue and Lucy not be animated, morphed, boolean-processed or
shown being damaged, and says "You can do anything you want to the Stanford bunny or the
armadillo." Khronos' sample repository reproduces the same paragraph as its `Stanford Graphics
Library` license reference.

What I measured:

| Model | Page says | Downloaded file header | Compressed size |
|---|---|---|---:|
| Bunny (`bun_zipper.ply`) | 35,947 v / 69,451 tris | same; properties x, y, z, confidence, intensity | 4,894,286 B |
| Happy Buddha (`happy_vrip.ply`) | 543,652 v / 1,087,716 tris | same | 14,456,495 B |
| Dragon (`dragon_vrip.ply`) | 566,098 v / 1,132,830 tris | 437,645 v / 871,414 faces | 11,197,764 B |
| Armadillo | 345,944 tris | binary big-endian, 172,974 v / 345,944 faces | 3,874,291 B |
| Lucy | 14,027,872 v / 28,055,742 tris | same (range request, header only) | 321,969,388 B |
| XYZ RGB Dragon | 3,609,455 v / 7,218,906 tris | 3,609,600 v / 7,219,045 faces | 70,527,166 B |
| XYZ RGB Thai statuette | 5,000,000 v / 10,000,000 tris | 4,999,996 v / 10,000,000 faces | 106,051,627 B |

All carry positions only: no normals, no UVs, no materials. The Dragon page figures do not match
the file (the README in the archive says it is a decimated reconstruction). The 871 K figure that
other projects quote is the file's real count. Links live under two roots: Bunny, Buddha, Dragon and
Armadillo under `http://graphics.stanford.edu/pub/3Dscanrep/`, Lucy and the XYZ RGB models under
`https://graphics.stanford.edu/data/3Dscanrep/`; the wrong root returns 404. The bunny archive
bundles range scans (22 MB uncompressed) beside the 69 K-triangle reconstruction.

### 2.2 Smithsonian Open Access 3D

Primary: `si.edu/openaccess/faq`, `si.edu/termsofuse` (read in a real browser; both return HTTP 403
to curl and to the fetch tool), `3d.si.edu` object pages.

The FAQ: Open Access items "carry what's called a CC0 designation. This means the Smithsonian
dedicates the digital asset into the public domain... you can use it for any purpose, free of
charge, without further permission from the Smithsonian." The Terms of Use add that the CC0 icon
"pertains only to the copyright status of the Content" and that the Smithsonian "does not guarantee
that all Content marked with the icon is free from rights other than copyright, including rights
held by third parties". The FAQ also excludes names, logos and trademarks and
culturally sensitive items. A search summary reported more than 3,000 3D models with over 90% CC0
(**Reported**, not counted by me).

Download path, observed on the Triceratops and Cher Ami pages: each object page links files at
`https://3d-api.si.edu/content/document/3d_package:<uuid>/resources/<file>`, in tiers: full
resolution OBJ zip, 150k OBJ and glTF zips, a 150k GLB, a 100k Draco GLB and USDZ. Plain `curl`
reached these URLs (HEAD 200, GET worked); the object pages themselves were not reachable from curl,
and one page (Apollo 11 Command Module) hit a Cloudflare interstitial even in a real browser. So
discovery is manual; the file URL plus a hash is what a setup script would pin.

Measured on the Triceratops 150k GLB (14,395,048 B): 149,998 triangles, 102,235 vertices,
`POSITION` + `NORMAL` + `TEXCOORD_0`, one material with base color, occlusion and normal JPEGs.
Full-resolution tiers are an order of magnitude larger: Triceratops OBJ zip 901,254,155 B holding a
3,879,154,851 B OBJ; Cher Ami OBJ zip 700,239,517 B holding a 2,435,332,756 B OBJ. I did not count
their triangles. A download of the Cher Ami archive reached only 245 MB after roughly 17 minutes
(one attempt, on a connection where the Stanford and Poly Haven downloads were fast), so
full-resolution tiers are not a casual setup step. Among the tiers listed on the two pages I read,
nothing sits between 150 K triangles and the multi-gigabyte tier.

### 2.3 Poly Haven models

Primary: `polyhaven.com/license`, the API terms in the `Poly-Haven/Public-API` repository,
`api.polyhaven.com/assets?t=models`, `api.polyhaven.com/files/<id>`, and downloaded glTF files.

Terms: "Our assets are all licensed as CC0, which is effectively Public Domain... You can use our
assets for any purpose, including commercial work. You do not need to give credit". The website
terms forbid "web scraping or data mining without express permission" and the API terms require a
unique `Referer` or user agent per application; neither limits fetching a known CDN file, which is
what Luminex already does for its HDRI (`dl.polyhaven.org`). Credit courtesy: `coast_rocks_02` and
`coastal_cliff_04` are photographed and processed by Rob Tuytel and cleaned up by Rico Cilliers.

The model list has 521 models. Selection by the API's `polycount` is unreliable, so I measured the
glTF of each of the 36 models with `polycount` of 300 K or more (accessor counts at the 1k tier):

| Model | API polycount | glTF triangles (measured) | Primitives / materials | Alpha modes | Total download, 1k |
|---|---:|---:|---|---|---:|
| `pine_tree_01` | 17,427,094 | 17,182,252 | 12 / 6 | `OPAQUE` | 958.1 MB |
| `fir_tree_01` | 7,853,731 | 6,982,937 | 12 / 6 | `BLEND`, `OPAQUE` | 487.4 MB |
| `pine_sapling_medium` | 9,784,670 | 6,038,139 | 9 / 3 | `OPAQUE` | 267.8 MB |
| `jacaranda_tree` | 312,356 | 3,863,832 | 3 / 3 | `BLEND`, `OPAQUE` | 214.6 MB |
| `island_tree_03` | 4,760,490 | 2,085,320 | 3 / 3 | `BLEND`, `OPAQUE` | 84.9 MB |
| `tree_small_02` | 4,652,585 | 2,062,487 | 3 / 3 | `BLEND`, `OPAQUE` | 101.0 MB |
| `coastal_cliff_04` | 2,883,111 | 1,537,926 | 1 / 1 | `OPAQUE` | 46.5 MB |
| `coast_land_rocks_02` | 2,420,895 | 1,291,146 | 1 / 1 | `OPAQUE` | 39.6 MB |
| `coast_rocks_02` | 2,363,290 | 1,260,423 | 1 / 1 | `OPAQUE` | 38.5 MB |
| `coastal_cliff_02` | 1,768,655 | 943,284 | 1 / 1 | `OPAQUE` | 29.2 MB |
| `celandine_01` | 1,746,764 | 8,966 | 5 / 1 | `BLEND` | 1.5 MB |
| `pine_sapling_small` | 398,144 | 398,144 | 6 / 2 | `MASK`, `OPAQUE` | 21.9 MB |

Take-aways: the rocks and cliffs are single-primitive, single-material, `OPAQUE` meshes, so they
exercise cluster LOD without material or alpha complications. Leaves in most tree models are
`BLEND`: of the 16 foliage-bearing models in Poly Haven's `trees` category, 10 use `BLEND`, 2 use
`MASK` and 4 are `OPAQUE` only (measured from each 1k glTF's materials). Luminex does not support
glTF `BLEND` (ADR 0018 on masked material coverage; a referenced `BLEND` material fails to load),
so those models would need conversion to `MASK`. `pine_tree_01` models needles as real geometry
and uses `KHR_materials_specular`, `KHR_materials_ior` and `KHR_texture_transform`, which the loader
must tolerate. Its file holds three separate trees placed side by side (three meshes and nodes of
6,951,524, 4,234,432 and 5,996,296 triangles; the largest primitive is 6.8 M triangles), so the
17.2 M total is not one mesh. `coast_rocks_02` uses no extensions.

`coast_rocks_02` in detail (all measured): `coast_rocks_02_1k.gltf` 2,810 B; `coast_rocks_02.bin`
35,867,060 B (shared by all texture tiers); three 1k JPEGs of 853,844, 785,725 and 947,627 B;
total 38,457,066 B. Mesh: 648,187 vertices, 3,781,269 32-bit indices, `POSITION` + `TEXCOORD_0` +
`NORMAL`, no tangents; 28.5 B per triangle uncompressed; extent roughly 46 m by 3 m by 49 m by the
accessor bounds. The API publishes an MD5 per file; my downloads matched all five. SHA-256 of the
downloaded files:

| File | SHA-256 |
|---|---|
| `coast_rocks_02.bin` | `ed06872c8d06f415cde38f6093dc555b175e73b54838f8f16c163c9bdfca05be` |
| `coast_rocks_02_1k.gltf` | `493b65e80b120deb6634dd1315add9fafb656616f1e27502494b6878613855c0` |
| `textures/coast_rocks_02_diff_1k.jpg` | `a1dc4e2d18679712dbab2e924eb2132a23ba8566b51734078fd2f62c2b78febe` |
| `textures/coast_rocks_02_arm_1k.jpg` | `ea492168ef17e3392974ae1b938f5c2b9297f00299e9450cab504332d8454e66` |
| `textures/coast_rocks_02_nor_gl_1k.jpg` | `3d5f52a5e53e18c4e85b7928bfe3ea13b6250831a4a52768700f96ae58a8098a` |

URLs: `https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/coast_rocks_02/coast_rocks_02_1k.gltf`,
the `.bin` under `.../gltf/8k/coast_rocks_02/coast_rocks_02.bin`, textures under
`.../Models/jpg/1k/coast_rocks_02/`. The `.gltf` references `textures/` and the `.bin` relatively, so
a setup script must lay the files out accordingly.

`coastal_cliff_04` follows the same layout (measured): `coastal_cliff_04_1k.gltf` 2,828 B, SHA-256
`645cadf494f60e247434451af6a88291d3be3019aa0973d635596810b0fe2d11`; `coastal_cliff_04.bin`
43,704,136 B, SHA-256 `f7c3a18acc848552b91b0de931ba8439d5f2a8849bd0d4ac718878aa4a90abcf`; 789,032
vertices, 4,613,778 32-bit indices, one `OPAQUE` material, no extensions, accessor extent about
87 m by 11 m by 24 m. Both files matched the API's MD5; its three 1k JPEGs (2,775,373 B by the API)
were not downloaded or hashed.

### 2.4 Three D Scans

Primary: `threedscans.com` home, info and object pages. Searching the raw HTML of all three for
"licen", "CC0", "public domain", "copyright" and "creative commons" found nothing. The info page
only lists institutions and says the project was initiated in 2012 by Oliver Laric. A 3DPrint.com
article quotes the project as offering scans "free and without copyright restrictions" and Laric
saying "I'm not claiming any copyright on them. They are free to circulate." (**Reported**). Without
license text on the site, publishing screenshots depends on a statement I could not locate. Measured
on `Oceanus100.stl.zip` (78,434,756 B): a 100,002,584 B binary STL with 2,000,050 triangles, no
shared vertices, no UVs, no materials. NVIDIA's `vk_lod_clusters` repository ships two derived scenes
(6.9 M and 7.9 M triangles) built from these scans (**Reported** by that repository's docs).

### 2.5 Khronos glTF Sample Assets

Primary: repository README, per-model READMEs, `LICENSES/`. The collection is "CC-BY-4.0" at the
repository level but each model has its own legal block. Measured: `Sponza` (Crytek's model with
PBR textures) 262,267 triangles, 103 primitives, 25 materials, `MASK` and `OPAQUE`, licensed under
the "CRYENGINE Limited License Agreement", whose text in the repository is a pointer to
`cryengine.com/ce-terms`. That conflicts with the CC BY 3.0 label the McGuire archive gives the same
model lineage (see section 3). `DragonAttenuation` has 134,995 triangles under the Stanford terms.
`VirtualCity` has 8,383 triangles under a 3DRT license that allows use "*only* in testing your glTF
tools" and forbids commercial deployment. The largest GLB or bin file in the repository is 43 MB
(`ABeautifulGame`, mostly textures); I did not count triangles for the other 147 models, but nothing
there is big enough to be dense. Not a source of dense content.

### 2.6 McGuire Computer Graphics Archive

Primary: `casual-effects.com/g3d/data10/index/index.js` (the model list) and each model's
`info.js`, plus HEAD requests on the zips. Each model's `info.js` carries its own license string.
Selected entries (triangles and licenses from `info.js`; sizes are real `Content-Length` values, because the
`downloadSize` strings in `info.js` are unreliable: Road Bike says 1.8 MB, real 29.6 MB; Bistro says
2.4 GB, real 1.44 GB; Power Plant says 97 MB, real 124 MB):

| Entry | Triangles | Real size | License string | Note |
|---|---:|---:|---|---|
| Bistro (Amazon Lumberyard) | 3,858,088 | 1,439,589,395 B | CC-BY 4.0 | exterior 2,837,181 + interior 1,020,907 |
| Power Plant | 12,759,246 | 124,310,853 B | Custom (UNC) | see below |
| Rungholt | 6,704,264 (12.3 M vertices) | 49,767,647 B | CC BY 3.0 | Minecraft map, regular tessellation |
| Hairball | 2,880,000 | 52,634,140 B | CC BY 3.0 | NVIDIA Research strands |
| Road Bike | 1,677,336 | 29,623,836 B | CC BY 4.0 | Mori |
| Gallery (Hallwyl Museum) | 998,941 | 64,961,580 B | CC BY-SA 4.0 | share-alike |
| BMW | 385,079 | 6,935,956 B | CC0 | Mike Pan, Blender demo |
| Bunny, Buddha, Dragon | 144,046 / 1,087,474 / 871,306 | 2.9 / 22.5 / 17.8 MB | "Stanford Scan" | same terms as section 2.1 |
| Dabrovic Sponza, Sibenik | 66,450 / 75,284 | small | CC BY-NC | non-commercial |

Bistro splits into `Exterior.zip` (64,186,766 B, a 311 MB OBJ), `Interior.zip` (21,293,389 B, a
108 MB OBJ) and three texture archives named `BuildingTextures`, `OtherTextures`, `PropTextures`
(no `.zip` suffix in the URL; 507,671,451 + 431,016,313 + 415,421,476 B). The MTL files define 132
exterior and 66 interior materials, of which 11 and 4 have `map_d` alpha maps. Luminex already has a
deterministic OBJ-to-glTF converter used for San Miguel, so this is the cheapest many-material scene
to add.

Power Plant: the original UNC page returned 404 today. A Wayback snapshot (2025-04-16) says: "No
commercial use of this model is allowed. This includes all commercial applications such as, but not
limited to, movies, games, as demo models for plotters, HUDs, virtual reality systems, etc." and
counts 12,748,510 triangles in 1,185 PLY files. Excluded.

### 2.7 NVIDIA ORCA

Primary: `developer.nvidia.com/orca` pages (fetched). The index calls ORCA "a legacy SDK". Bistro:
"Creative Commons CC-BY 4.0", interior 1,046,609 triangles, interior with wine 1,293,691, exterior
2,832,120, FBX plus a Falcor scene file. Emerald Square (artist Nicholas Hull): "Creative Commons CC
BY-NC-SA 3.0", 10,046,405 triangles. SpeedTree trees and plants: CC BY-NC-SA 3.0, FBX. Beeple
Zero-Day: CC-BY 4.0, 1,372,670 and 1,294,866 triangles. Downloads are not login-gated: each page's
Download link is a stable path (`developer.nvidia.com/bistro`, `.../emerald-square`) that redirects
to a zip on `developer.download.nvidia.com` with a short-lived token in the query string, and an
anonymous request reached HTTP 200 for both (`Bistro_v5_2.zip`, 894,377,473 B;
`EmeraldSquare_v4_1.zip`, 617,345,248 B; headers only, archives not downloaded). ORCA is therefore
the original home of Bistro, as FBX; the McGuire copy is a remastered OBJ export with plain static
URLs, which suits Luminex's existing OBJ converter. Triangle counts differ slightly between the
ORCA FBX and McGuire's remastered OBJ.

### 2.8 Intel Sponza and Jungle Ruins

Primary: Intel's samples page (read in a browser; plain curl gets Access Denied) and the archives'
own `credits_license.txt`, read by range-fetching the zip members.

The page: "Downloads are available under the Creative Commons Attribution license." The Sponza
archive's `credits_license.txt` has, directly under its one-line credit: "For personal use and
educational use. Limited commercial use for marketing and print purposes.", then citation and
credit lists, then the full CC BY 4.0 legal code. The base archive and the curtains, ivy and trees
add-ons carry the same 15,921-byte file. Those two
statements appear inconsistent (CC BY 4.0 does not allow additional restrictions on reuse), and I
cannot resolve which governs. The Jungle Ruins `credits_license.txt` has only the CC BY 4.0 text.

Measured from the glTF JSON inside the zips (accessor counts, no full downloads):

| Pack | Zip size | Triangles | Notes |
|---|---:|---:|---|
| Sponza base | 3,987,608,266 B | 3,747,018 | 115 meshes, 405 primitives, 28 materials, 72 images, `BLEND` + `OPAQUE`; zip holds 6.71 GB (FBX, USD, 3ds Max, glTF) |
| Curtains | 786,898,766 B | 1,997,366 | 43 primitives, 4 materials |
| Ivy | 1,383,921,436 B | 5,065,558 | 2 primitives; the page says "full geometric detail" |
| Trees | 951,147,802 B | 2,023,747 | cypress leaves are alpha cards (`BLEND`) per the page |
| Jungle Ruins | 4,254,165,506 B | not measured | Blender 4.2 + USD only |

Base plus all three add-ons is 12,833,689 triangles (sum of the four glTF counts). Jungle Ruins is
advertised as having "an over one trillion total triangle count" (the page's wording); the included
help document gives no counts, and the scene scatters instances with Blender add-ons, so the figure
presumably counts instances (**inference**). It has no glTF or FBX, and Luminex has no USD loader.
The `cdrdv2.intel.com/v1/dl/getContent/<id>` links 302 to short-lived signed CloudFront URLs, so the
ID is stable but the final URL is not.

### 2.9 Disney Moana Island

Primary: the dataset page and `License.txt`. Terms: the scene "may only be used for research or
software development (including benchmarking) purposes"; Disney names may not be used to imply
endorsement of products or of "benchmarking results obtained from this scene description"; "The
name "Moana" may NOT be used except as required to identify the scene description, and the scene
description and its output may only be referred to as the "Moana Island Scene"". Sizes:
Base 45 GB download (measured 48,115,580,341 B), USD 15 GB (measured 15,159,731,761 B), PBRT 6 GB,
Animation 24 GB. Wrong scale and wrong content type (instanced curves, volumes) for a solo setup
step. Excluded.

### 2.10 Quixel Megascans, Fab and Unreal sample content

Primary: `fab.com/eula` (Fab EULA, last updated 2024-10-01), `unrealengine.com/eula/content` (Epic
Content License Agreement), two Fab listings.

The Fab page's summary, which the page itself calls "for your convenience only and ... not legally
binding", says a Standard License lets you "Use the assets with any compatible tools (usage is not
limited to Unreal Engine)" and forbids you to "Resell or redistribute the asset for free on a
standalone basis". The binding text (sections 3 to 6) never names an engine for Standard License
content: it grants a license to "privately use, reproduce, display, perform, and modify the
Content"; subject to section 6, you may freely distribute "rendered video files... and images
created using Content" (section 4); you "may not Distribute Content on a standalone basis to third
parties except to your collaborators", for example in a private repository (section 5); and you may
not make Content available "in world- or level-editing tools or templates" (section 6). Section 6
also bars use of "NoAI Content" in datasets, development or training inputs of "Generative AI
Programs"; section 16(l) defines NoAI Content as content tagged "NoAI" at the time of the
transaction and Generative AI Programs as AI, machine learning or neural-network technologies
"designed to automate the generation of or aid in the creation of new content", excluding programs
that "solely operate on the original content", classify it or arrange existing content.

The Epic Content License Agreement is a separate instrument for content obtained from Epic
directly (it names the Unreal Engine Marketplace, the launcher's Learn tab and the Quixel Megascans
library). It defines "UE-Only Content" as content "designated as only permitted for use in
conjunction with Unreal Engine and Unreal Engine-based products", and its Megascans addendum says
Megascans acquired while an account is enrolled in an Unreal Engine plan "may only be used and
shared as UE-Only Content". So engine exclusivity is real but depends on the license an asset was
acquired under: UE-only for Megascans taken under that plan and for items designated UE-Only, not
for a Fab Standard License purchase.

Listings fetched on 2026-10-01: a Megascans rock (Forest American Rock Boulder Stone Large 01):
Standard License, formats `fbx`, `glb`, `usdz`, `gltf`, a 484.31 MB zip, priced from $4.99, "Allows
usage with AI: No". City Sample (Epic's free Matrix Awakens project): Standard License, included
format "Unreal Engine" only, "Allows usage with AI: No", engine versions 5.0 to 5.8; the listing
text says "this version of City Sample is built entirely in-engine using the PCG framework". The
Megascans seller page lists 22 K products, with some marked free. Before Fab, Megascans was reported
to be free only inside Unreal projects, and Fab-era Megascans free for all engines until the end of
2024 (**Reported**: CG Channel, 2024; a search summary of a third-party guide for the earlier
terms). Conclusion: the Fab Standard License does not forbid use outside Unreal and permits
publishing rendered images, but nothing here is anonymously downloadable, hashable from a public
URL, or redistributable in a public setup script, so a Fab asset could only ever be a privately held
fixture that other people cannot reproduce. The NoAI flag could matter if the N1 learned-rendering
studies train on this content; whether an in-shader inference lab counts as a "Generative AI
Program" is a legal reading I did not make. Bevy's maintainer used a Megascans asset (Huge Icelandic
Lava Cliff) as a benchmark mesh, so this class of content is used in practice, just not as a
reproducible public fixture.

### 2.11 Blender demo files and other datasets

Primary: `blender.org/download/demo-files/` and HEAD requests. Classroom (Christophe Seux, CC0;
`download.blender.org/demo/test/classroom.zip`, 70,279,690 B), Agent 327 Barbershop (CC-BY, 280 MB
per the page), Pabellon Barcelona (CC-BY, 24,661,092 B), Lone Monk (36,695,778 B; no license shown on
that page). The URLs carry stable 2015 to 2021 `Last-Modified` dates. All are `.blend`, so a headless
Blender export step (a new tool dependency) is needed; I did not count their triangles
(**[UNVERIFIED]**).

Helsinki Kalasatama, Zenodo record 7599228 (CC BY 4.0, DOI 10.5281/zenodo.7599228, published
2023-02-02, Aalto University with the City of Helsinki): `Kalasatama_30M_8kTex_OBJ.zip`
6,038,622,934 B, MD5 `310127f6fc500be67c185f0761413fdf`, "30 million polygons" and 50 8k PNG
textures, from terrestrial laser scanning plus UAV photogrammetry processed in RealityCapture
without manual editing. The only source here with a persistent identifier and a published checksum.

NVIDIA Zorah (the `vk_lod_clusters` docs): 1.63 G triangles, 18.9 G with instancing; geometry-only
glTF 7.22 GB as 7z (9.32 GB unpacked, a 26 GB render cache); textured version 70 GB as 7z. The docs
say "the asset itself is licensed under MIT License" (**Reported**; not checked against NVIDIA's
license text). Processing needs roughly 45 to 60 GB of RAM (Kapoulkine's measurements). Out of scale
for this project except as a reference point.

Sketchfab: a search summary says its download API requires an authenticated account and API token
(**Reported**). Not suitable for unattended setup.

## 3. License risk register

| Risk | Where | Why it matters here |
|---|---|---|
| High | Stanford models | Research-only, non-commercial; images only in "a scholarly article or book"; a public portfolio is not clearly either. Cultural-use rules for four of the models. |
| High | Emerald Square, SpeedTree (CC BY-NC-SA) | NC; SA would also bind derived images. |
| High | Power Plant | "Games" and "demo models" named; original site 404. |
| High | Intel Sponza | Header restricts to "personal use and educational use" over CC BY 4.0 text. |
| Medium | Moana Island | Research and benchmarking are permitted; naming constraints apply to published results. Excluded mainly for size and content type. |
| Medium | San Miguel (already fetched by the optional setup step) | Two upstream records differ: the archive's `info.js` says CC BY 3.0; the `license.txt` inside the zip says "free for research and educational use with attribution". The project already records both; which governs is unresolved (see below). |
| Medium | Crytek Sponza (already in the repo) | McGuire labels it CC BY 3.0 (2010 Frank Meinl, Crytek); Khronos' copy cites the CryEngine Limited License Agreement. |
| Medium | Three D Scans | No license text on the site; only press quotes. |
| Medium | CC BY-SA assets (Gallery) | Share-alike would reach screenshots and baked data. |
| Low | Bistro, Rungholt, Hairball, Road Bike, Blender CC BY | Attribution only. Record it in `LICENSE.txt` and in captions. |
| Low | Poly Haven, Smithsonian, Blender CC0, BMW | CC0. Smithsonian excludes third-party rights, trademarks and sensitive items; Poly Haven's site terms bar scraping but not pinned file fetches. |
| Operational | Megascans, Epic samples | The Fab Standard License names no engine, but login, purchase, no standalone redistribution, NoAI flag; content acquired as UE-Only under the Epic Content License Agreement stays Unreal-only. |

San Miguel, stated exactly (all read on 2026-10-01):

- Upstream archive page: each entry is rendered from that model's `info.js`, and the page has no
  site-wide license statement. San Miguel's `info.js` gives the title "San Miguel 2.0", an update
  date of 2017-07-02, the copyright holder Guillermo M. Leal Llaguno, and the license as a link to
  CC BY 3.0. The live file is byte-identical to the copy setup stores, and its SHA-256 matches the
  pin in `xmake/setup.lua`.
- Inside the pinned zip: `license.txt` (dated 2017-05-25 in the zip directory) is headed "San
  Miguel 2.1" and says: "San Miguel, modelled by and copyright Guillermo M. Leal Llaguno", "2017
  version improved by ... with permission from Mr. Llaguno", and "This model is free for research
  and educational use with attribution." It does not mention Creative Commons, commercial use or
  publication of images.
- What the repository records: setup stores both files verbatim beside the converted scene, and the
  provenance file's license field reads "Archive metadata: CC BY 3.0; preserve the accompanying
  LICENSE.txt terms verbatim". `THIRD_PARTY_NOTICES.md` lists both records and says the
  "metadata/version discrepancy is preserved, not resolved by the local conversion", asking that
  both records and credits stay with the asset and with derived comparisons; the temporal comparison
  guide notes the same two records. `Assets/README.md` describes Damaged Helmet and Sponza only and
  does not mention San Miguel. The gallery images that the notices file lists as derived from
  fetched assets are of Sponza and Damaged Helmet; none is of San Miguel. Whether San Miguel images
  have been published outside the repository (release attachments, portfolio pages) was not checked.

So this is a known, documented discrepancy, not a newly found one. Both records allow use with
attribution, which the project keeps; they differ in scope. CC BY 3.0 covers any use, including
publication and commercial use. The enclosed text names research and educational use only, so if
it governs, published San Miguel screenshots and measurement tables rest on a public portfolio
counting as "research and educational use", which the text does not define. Nothing I read says
which record the rights holder intends, and I did not ask the archive maintainer. The repository
documents the discrepancy; I found no recorded decision on how to read it for publication.

## 4. How dense-geometry systems are benchmarked

Sources: the Nanite SIGGRAPH 2021 slides, Unreal Engine source (branch `ue5-main`, read
2026-10-01, described in my own words), three Bevy virtual-geometry write-ups, NVIDIA's
`vk_lod_clusters` documentation, two meshoptimizer-author posts, the 2015 GPU-driven rendering
talk, two open Nanite-like repositories, and three research papers.

### 4.1 Scene construction

| Project | Scene recipe | Evidence |
|---|---|---|
| Nanite demo ("Lumen in the Land of Nanite") | hand-built environment, 433 M source triangles; a dynamic-resolution frame near 2496x1404 upsampled to 4K | Karis, SIGGRAPH 2021 |
| Bevy 0.14 | 3,092 copies of the Stanford bunny (144,042 triangles each, 4,936 clusters in the LOD tree); 5 unique PBR materials, the rest one debug material; 2240x1260; RTX 3080 locked to base clocks; averaged over 10 frames | jms55.github.io, 2024-06-09 |
| Bevy 0.15 | 3,375 bunnies in a 15x15x15 cube; plus 847 Quixel "Huge Icelandic Lava Cliff" scans in an 11x11x7 block (32,288 clusters per instance); same GPU and resolution | jms55.github.io, 2024-11-14 |
| NVIDIA `vk_lod_clusters` | `--gridcopies N` copies of a model on a grid; default scene the Stanford bunny; two statue scenes of 6.9 M and 7.9 M triangles; Zorah 1.63 G triangles | repo README, `docs/scenes.md` |
| Light-system (Vulkan, Apple M4 via MoltenVK) | Stanford Dragon (871 K), a generated 1M-triangle "Massive City"; planned scene classes: photogrammetry ruins, dense architecture, rock field with heavy instancing, indoor occlusion stress, vegetation | repo README, `BENCHMARK_PLAN.md` |
| Render-Tech-Lab (WebGPU) | S0 to S5 load curve: baseline, 500 and 1,000 instanced objects, 2,000 unique objects, 30 dynamic lights, a hostile combination; looks for the knee of the curve | repo README |
| Epic City Sample | procedurally built city (the current Fab listing, for engine versions 5.0 to 5.8, says it is built in-engine with PCG) | Fab listing |
| Luminex VisibilityLab | seeded grid, N from 1 to 1,048,576 instances, 12-second rail, paired AB/BA runs | project docs |

Camera paths. NVIDIA's `--runcamerapath <index> <framecount>` plays a keyframed path across
exactly that many rendered frames "independent of frame rate or GPU speed", with Catmull-Rom
smoothing optional and paths stored in a text file next to the scene. Luminex's rail with
`--measure-camera track` has the same purpose.

Two pitfalls from these projects bear on scene design. First, Bevy's 0.15 post reports an overflow
at 1,042 cliff instances: the visibility buffer packed a 25-bit cluster ID, and 1,042 x 32,217
clusters exceeds 2^25 (33,554,432). The fault only appeared when instance count was swept past a
packing limit. Second, Karis reports that fixing scale-invariant error made "the room of statues"
(previously 2 to 3 times the cave scene's cost) rasterize the same number of triangles as the cave:
two scenes of very different source complexity are a direct test of the constant-work claim.

### 4.2 Counters reported

Nanite's own statistics display (shader and CPU code in `Nanite.cpp` and `NanitePrintStats.usf`,
`ue5-main`; enabled with the `r.Nanite.ShowStats` cvar or the `NaniteStats` command, with
`r.Nanite.StatsFilter` selecting one raster pass) prints, separately for the main and post-pass
culling phases: hierarchy chunks, instances before and after culling, node visits, candidate
clusters, clusters sent to the software and hardware rasterizers, and in the totals section,
rasterized triangles and vertices, pixel and quad evaluations with a helper-lane fraction, and
raster and shading bin counts. It also reports primary-view and total-view counts (shadow views run
the same culling and raster passes). I read only `ue5-main`; I did not compare it with `5.8` or
`ue6-main`.

The one fully published frame of such numbers is Karis' slide (a table whose text extraction
interleaves two columns, so I give the numbers as read and flag the ordering):

| Stage | Main pass | Post pass |
|---|---:|---:|
| Instances before culling | 896,322 | 102,804 |
| Instances after culling | 3,668 | 365 |
| Hierarchy node visits | 39,274 | 19,139 |
| Candidate clusters | 1,536,794 | 458,805 |
| Visible clusters, software raster | 184,828 | 7,370 |
| Visible clusters, hardware raster | 6,686 | 536 |

Total rasterized: 199,420 clusters, 25,041,711 triangles, 19,851,262 vertices (the four cluster
counts do sum to 199,420). The slide says the same frame through UE4's standard path "would have to
rasterize over a billion triangles" and that the 25 M "is consistent throughout the demo". Timings:
about 2.5 ms to build the visibility buffer and about 2 ms for the deferred material pass at roughly
2496x1404. The slide labels main-pass instance culling "108ms"; given the 2.5 ms total it must be
108 microseconds (**inference**, a slide typo).

Other counters in the sources:

| Counter | Reported by | Value or use |
|---|---|---|
| Source and DAG triangles | Karis; Kapoulkine | 433 M source, 882 M Nanite triangles (about 2x); Zorah 1.63 G source, about 3.26 G DAG |
| Per-pass GPU milliseconds | Karis; Bevy | Bevy 0.15 bunny scene: fill cluster buffers 0.12, first cull 0.19, software raster 0.42, second cull 0.06, depth passes 0.03 to 0.04 each; total 0.93 ms. Bevy 0.14 same scene 4.97 ms |
| Cull cost versus cluster count | Bevy 0.15 | cliff scene (many clusters, few instances): first cull 1.27 ms against 0.19 ms for bunnies |
| DAG shape and cluster fill rate | Bevy 0.15 | per-level cluster counts and the share of full clusters; bunny: 7 levels ending at 19 clusters in 0.14 against 12 levels ending at 1 in 0.15 (ideal is halving per level); worst-level fill rate 20% in 0.14 against 76% in 0.15 |
| Bytes per triangle, memory and disk by data class | Karis; Bevy 0.15 | Karis: 5.6 B per Nanite triangle, 11.4 B per input triangle on disk (about 10.9 MB per million input triangles), 7.67 GB memory format for the demo. Bevy: bunny 3.61 MB disk and 4.5 MB memory, cliff 49.83 MB disk and 63.0 MB memory, broken down into positions, normals, UVs, indices, clusters, spheres, errors |
| Offline build time, peak RAM, CPU utilization | Kapoulkine, 2025 | Zorah on a 16-core desktop: about 30 minutes at the start, 2m 35s after thread-ordering, sparse-vertex and arena-allocator fixes; peak about 45 to 60 GB; CPU use measured with `/usr/bin/time` |
| Full-pipeline cost | Kapoulkine; NVIDIA | GPU ms for raster versus ray tracing on a low-end GPU (16 ms and 26 ms, RTX 3050, about 2 GB geometry pool) |
| Cull efficiency | Haar and Aaltonen, 2015 | percent of triangles removed: 20 to 40% (backface plus cluster bounds), 30 to 80% of shadow triangles; draw calls down by one to two orders of magnitude |
| Selected percentage and speedup | Virtualized 3D Gaussians (2025) | percent of primitives selected, FPS speedup rate and FLIP error versus the full model, over camera trajectories at several distances |

Derived values (my arithmetic from the quoted figures): Bevy's bunny is about 31 B per source
triangle in memory and 25 B on disk across all LODs; Zorah's cache is about 17 B per cluster
triangle (54.9 GB over 3.26 G) and 34 B per source triangle, with full attributes. NVIDIA reports
the two statue scenes at about 1.3 to 1.4 GB preloaded for 7 M to 8 M triangles (about 177 B per
triangle), a figure that includes ray-tracing data and is not comparable to Nanite's compressed
disk format.

### 4.3 LOD error thresholds and quality metrics

| System | Metric and default |
|---|---|
| Nanite | Pixel-level error: the `r.Nanite.MaxPixelsPerEdge` cvar (`NaniteCullRaster.cpp`, `ue5-main`) defaults to 1.0 and sets the triangle edge length in pixels the runtime aims for; separate scaling cvars let primary and shadow raster raise it when over a per-frame time budget (`r.Nanite.PrimaryRaster.TimeBudgetMs`). Karis: below 1 pixel of error, clusters are "imperceptibly different". |
| Bevy | A cluster is accepted when its projected group error is under 1 pixel. |
| NVIDIA `vk_lod_clusters` | Conservative angular error from the arcsine of the group's error radius over distance to the closest point of its bounding sphere; the threshold is one pixel's field of view at the projection center, so it does not vary across the image. |
| 3D Tiles (CesiumJS) | Screen-space error from a tile's `geometricError`; `maximumScreenSpaceError` defaults to 16 pixels. |
| meshoptimizer `clusterlod.h` | Documents screen-space error as bounds error over distance (0 to 1, multiplied by screen height to get pixels). |

For comparison of images, I found no Nanite-like triangle renderer that publishes an
image-difference metric against its own full-resolution mesh. The three documented patterns I found
are:

- Rendered image against a reference render with FLIP. The Virtualized 3D Gaussians paper (a
  Nanite-inspired cluster LOD system for Gaussians, not triangles) reports FLIP, selected-primitive
  percentage and speedup per camera trajectory and distance (primary: arXiv 2505.06523, tables read
  in the PDF). Luminex already has CPU LDR-FLIP in its temporal
  comparison tooling, where it is reported beside null controls without an acceptance threshold,
  and exact-image gates in its screenshot comparator. An image rendered from a simplified cut
  differs from the full-resolution image in some pixels, so the exact gate cannot be applied to
  LOD-on output as is; the implications section lists the choices that leaves.
- Full-reference image-quality comparison for LOD reduction in general, with a convolutional
  classifier for "popping" (Tamm et al., EA SEED, arXiv 2208.12674; discrete LOD; the paper surveys
  full-reference metrics such as SSIM and LPIPS and defines popping as an abrupt, visible change at
  an LOD transition, especially in the silhouette).
- Temporal flicker across a transition: Li et al. (arXiv 2309.11591) use the reference-based
  flicker metric attributed to Winkler et al.: compute the difference between each processed frame
  and its reference for two consecutive frames, subtract the two difference images, take the 2D DFT,
  sum low and high radial-frequency bands, and add them. It measures popping without a human, and
  needs a reference render of every frame in the sequence.

### 4.4 Method hygiene seen in the sources

- Fixed device and state: Bevy locks the GPU to base clocks. I found no equivalent clock-lock
  control for Apple GPUs (not researched; **[UNVERIFIED]**); Luminex's answer is paired alternating
  runs.
- Warm versus cold streaming and cache state are recorded (light-system plan; Kapoulkine measured
  with warm file caches and noted a 10 s fopen cost for a stale 62 GB cache file).
- Per-pass timings come from a single capture or averaged frames; Bevy states "averaged over 10
  frames", Karis reports per-pass averages over the demo.
- Hardware, resolution, driver, build ID and date are stored per run (light-system plan).
- Render-Tech-Lab archived its own earlier S3 and S5 timings as "unverified" because they were
  hard-coded in the harness. Treat unreproduced figures as unusable.
- Compare only within a post: Bevy's two posts use different bunny counts (3,092 and 3,375).

### 4.5 What the Metal API can report on this machine

A small probe run on the recorded development machine (Apple M3 Max, 40 GPU cores, macOS 26.7, Xcode
26.6, `supportsFamily(.metal4)` true) enumerated `MTLDevice.counterSets`: only the timestamp set
(`GPUTimestamp`). Sampling support: `atStageBoundary` true; `atDrawBoundary`, `atDispatchBoundary`,
`atTileDispatchBoundary`, `atBlitBoundary` all false. The SDK header declares statistic counters
(`ClipperInvocations`, `ClipperPrimitivesOut`, `FragmentInvocations`, `FragmentsPassed`,
`VertexInvocations`, `ComputeKernelInvocations`), but this device does not expose that counter set
through the public API. Whether Xcode's GPU counters or Instruments can show rasterized-triangle or
fragment counts for a frame was not tested here (**[UNVERIFIED]**). Consequently, Nanite-style stage
survivor counts must be atomic counters written by the culling and raster-setup shaders and read back
at frame retirement, as Luminex's visibility counters and its independent ID oracle already do.

## 5. Recommended GeometryLab fixture set

Aim: a small, hash-pinned set that follows the existing fetch pattern (pinned URL, SHA-256,
`LICENSE.txt` and provenance next to the asset, nothing committed), is CC0 or CC BY only, and covers
the four content classes. This is my recommendation from the evidence above, not a settled plan.

| ID | Fixture | Source | License | Download | Triangles | What it proves |
|---|---|---|---|---:|---:|---|
| A | Rock slab `coast_rocks_02` (1k glTF) | Poly Haven | CC0 | 38.5 MB | 1,260,423 | One dense photogrammetry surface with UVs and PBR: DAG quality (levels, fill rate), bytes per triangle, build time, error versus the full-resolution mesh, popping along a rail. Default fetch. |
| B | Cliff wall `coastal_cliff_04` (1k glTF) | Poly Haven | CC0 | 46.5 MB | 1,537,926 | A second shape (about 87 m long, 24 m deep and 11 m high by the glTF accessor bounds): large occluders and horizon, 22% more triangles than fixture A, so the sweep is not tuned to one asset. Default fetch. |
| C | Field | procedural | n/a | 0 | N x 1.26 M | A seeded grid or scatter of fixtures A and B with random yaw and scale, N from 1 to roughly 16 K (up to about 20 G nominal source triangles with shared mesh storage): constant-work claim, instance-culling scale, cluster-ID capacity, cost versus N. Reuses the VisibilityLab pattern. |
| D | Bistro (exterior and interior) | McGuire copy of Amazon Lumberyard | CC BY 4.0 | 1.44 GB, optional flag | 3,858,088 | A real scene with 198 materials (15 alpha-cutout) and mixed scale: material-path cost (the surface-path experiments), cluster LOD across many small meshes, first non-San-Miguel real-scene check. Same OBJ-to-glTF converter as San Miguel. |
| E | San Miguel (existing) | McGuire | CC BY 3.0 by archive metadata; the enclosed text differs (section 3) | existing optional 511 MiB | 5,617,451 | Alpha-masked foliage, 281 materials, one un-instanced 5.6 M-triangle mesh: how clusters treat aggregate geometry and masked leaves; the failure-mode scene. |
| F | `pine_tree_01` (1k glTF) | Poly Haven | CC0 | 958 MB, optional flag | 17,182,252 | Geometric foliage (needles are real triangles, no alpha cards): the same problem Zorah's leaves pose, with three tree meshes of 4.2 M to 7.0 M triangles (17.2 M in one file) as a build-time and memory test. Optional. |

Download totals: default (fixtures A and B) is 85 MB (38,457,066 + 46,482,337 B). Adding Bistro
makes about 1.52 GB; adding the pine tree about 2.5 GB; San Miguel's 511 MiB is already part of the
current optional set. A minimal variant is fixture A alone (38.5 MB) plus the procedural field and
the existing San Miguel. The IDs A to F label fixtures in this notebook only; they are not roadmap
milestones.

Procedural field specification (to make numbers reproducible): a seeded generator (as in
VisibilityLab) producing instance transforms from a documented seed, grid pitch relative to the
asset's bounds, bounded random yaw and uniform scale, optional mixing of fixtures A and B; N is a
command-line parameter; the manifest records seed, N, the asset hashes and the camera rail.

Measurement protocol proposal (extends the existing paired AB/BA design: fresh processes, 12
repetitions, 32 warmup and 256 measured frames, seeded bootstrap intervals; a frozen reference
binary):

| Quantity | How to obtain | Why |
|---|---|---|
| Source triangles (N x asset) and scene instance count | manifest | the denominator |
| Instances in and out of culling; node visits; candidate clusters; clusters and triangles after frustum, after occlusion; clusters rasterized | atomic counters in the cull and raster-setup shaders, read back at retirement; an independent CPU oracle on sampled frames, as with the occlusion ID oracle | Metal exposes no statistic counters here (section 4.5) |
| Per-pass GPU ms | existing per-pass timestamps | stage-boundary sampling is what the device supports |
| Memory and disk by data class; bytes per source triangle; build wall time, peak RSS, CPU utilization on the frozen device | offline baker report | compare with Karis (11.4 B), Bevy (31 B in memory), Kapoulkine (build time) |
| Cluster-ID and buffer-capacity headroom | counter versus capacity | Bevy's overflow at 2^25 |
| Image error versus full-resolution reference | ordinary raster path as reference at close, mid and far poses, LDR-FLIP mean and 99th percentile, at error thresholds of 1, 2 and 4 pixels | the quality side of the trade-off curve |
| Popping | flicker metric along the rail with temporal reconstruction off (so history does not hide or add pops), also repeated with it on | LOD stability; the ordinary path's own flicker is the baseline |
| Constant-work check | rasterized clusters and triangles versus N and versus source complexity (a field of fixture A against San Miguel) | the headline claim |

Estimates (arithmetic, not measurements): at Nanite's 11.4 B per input triangle, the cluster data
for `coast_rocks_02` would be about 14 MB; at Bevy's 31 B it would be about 39 MB. At 128
triangles per cluster and assuming every cluster is full, `coast_rocks_02` has about 9,850 leaf
clusters and about 19,700 in the full DAG (twice the leaves, the halving-per-level ideal). Under a
scheme like Bevy 0.15's, which numbers every cluster of every instance in one 25-bit field (2^25 =
33,554,432), that overflows near 1,700 instances (33,554,432 / 19,700); partly filled clusters
raise the per-instance count and lower that limit. The field sweep therefore exercises the ID
design directly.

License flags for this set, stated plainly: fixtures A, B and F (CC0) carry no license risk; the
operational risk is Poly Haven moving or replacing the files, which the pinned hash turns into a
loud setup failure. Bistro needs attribution to Amazon Lumberyard and the McGuire archive. San
Miguel already carries the two differing upstream records described in section 3; the recommended
set does not make that worse, but publishing its screenshots depends on which record governs. Do not
add the Stanford models to published results without a decision on the "scholarly article or book"
clause; if they are wanted for cross-project comparability, keep them out of the default fetch and
out of published images.

## Corrections to earlier research

- The earlier open-source survey carries "Stanford Dragon (871K tri)" from the light-system README
  and "Emerald Square (149,998 triangles)" from Render-Tech-Lab. Both are accurate for what those
  projects loaded (871,414 faces in the Stanford file; a 149,998-triangle slice of Emerald Square),
  but the Stanford page lists 1,132,830 triangles and the whole Emerald Square scene is 10,046,405
  triangles under CC BY-NC-SA 3.0.
- The brief describes San Miguel as "millions of triangles": the repository's converted scene is
  measured at 5,617,451 triangles in one mesh and one node.
- Expectation that Megascans and Unreal content cannot be used outside Unreal: too broad. Content
  bought on Fab under the Fab Standard License is not tied to an engine; content designated
  UE-Only under the Epic Content License Agreement, including Megascans acquired under an Unreal
  Engine plan, is. See section 2.10.
- Outside my notebook's scope but seen in passing: the pipeline survey dates meshoptimizer v1.0,
  v1.1 and v1.2 to 2023-12-08, 2024-04-02 and 2024-06-30. The GitHub releases API shows v1.0 on
  2025-12-08, v1.1 on 2026-04-02, v1.2 on 2026-06-30 and v1.3 on 2026-09-25. Kapoulkine's 2025 post
  says the Nanite-style example has existed "since 2024" and that `clusterlod.h` is the 2025
  extraction of it. Recheck in the construction notebook.

## Implications for Luminex

- No content in the repository today needs virtualized geometry to render: Sponza is 262 K
  triangles and San Miguel 5.6 M in one mesh. A cluster milestone needs either a dense single mesh
  or instancing, and the cheapest CC0 option is Poly Haven (about 38 MB per million-triangle asset).
- Instancing a shared mesh keeps the reference path viable: the reference image for N instances needs
  one vertex pool and N rows of the existing 240-byte instance table, not N copies.
- All cluster counters have to be shader-written because this device exposes only timestamps. This
  matches the project's existing ID-oracle practice and should be part of the first slice's design.
- The exact-image gates used elsewhere cannot be applied unchanged to images rendered from a
  simplified cut. Two ways to gate a cluster slice follow, and they can be combined:
  - Keep the existing practice of exact gates plus independent oracles. The exact gate compares the
    cluster path with LOD forced to the full-resolution clusters against ordinary raster; an
    independent CPU oracle checks the selected cut (each drawn cluster within the error threshold,
    its parent not, no gaps or overlaps) and the shader-written counters; LOD-on image error and
    flicker are measured and published but not gated. This keeps every gate exact and causal, and
    leaves visible LOD quality to review. Whether the forced full-resolution path is bit-exact
    against ordinary raster is not established and would be the first thing to test.
  - Add a perceptual tolerance gate on LOD-on output, built on FLIP and a flicker metric. This
    gates what a viewer sees, but the thresholds would be the project's own: no triangle renderer I
    read publishes such a metric to calibrate against, a tolerance can absorb a real regression,
    and it departs from the project's record of exact gates with scoped, documented exceptions.
  Either way, a constant-work check across scenes of different source complexity is a counter
  comparison, not an image gate.
- Cluster ID and buffer capacity limits should be derived from the field sweep, not assumed.
- The frozen-device protocol stays as is; without a known way to lock Apple GPU clocks
  (**[UNVERIFIED]**), pairing and warmup carry the load that Bevy's locked clocks carry elsewhere.

## Open questions and what could not be confirmed

- Triangle counts of the Smithsonian full-resolution tiers (a 700 MB download was abandoned at 245
  MB for time) and of the Blender Classroom and Barbershop scenes.
- Whether any Smithsonian object lies between the 150 K tier and the multi-gigabyte tier; object
  pages are hard to reach from scripts.
- Which of the two Intel license statements governs Sponza and its add-ons; I did not contact Intel.
- The original UNC Power Plant page (404; snapshot only).
- Which of the two San Miguel records governs (CC BY 3.0 in the archive metadata, or "research and
  educational use with attribution" in the enclosed text); see section 3.
- Which license governs a given Megascans asset depends on how and when it was acquired; I read
  the Fab EULA and the Epic Content License Agreement but did not test an account or a purchase.
- Zorah's MIT license is a repository claim; the textured 70 GB version was not inspected.
- Whether Xcode GPU counters expose rasterized primitive or fragment counts on M3 Max.
- Poly Haven appears to replace files under the same URL: the `pine_tree_01` `.bin` carries an upload
  timestamp of 2025-05-28, almost two years after the asset's 2023-06-23 publication date
  (**inference** from the storage header; I did not see the earlier bytes). The API publishes an
  MD5 and a `files_hash` per asset; the observed bytes matched, and a pinned SHA-256 would detect a
  change, at the cost of a setup failure until the pin is updated.
- Wihlidal's GDC 2016 compute-pipeline slides (per-stage triangle survival tables) could not be
  fetched; the S3 link returned access denied. Not cited.
- Pre-Fab Megascans terms were not read from a primary source (**Reported** only).

## Sources

Content and licenses

- Stanford 3D Scanning Repository: https://graphics.stanford.edu/data/3Dscanrep/ ; files under
  http://graphics.stanford.edu/pub/3Dscanrep/ and https://graphics.stanford.edu/data/3Dscanrep/
- Smithsonian: https://www.si.edu/openaccess/faq ; https://www.si.edu/termsofuse ;
  https://3d.si.edu/ ; object pages for Triceratops
  (`3d.si.edu/object/3d/triceratops-horridus-marsh-1889:d8c623be-4ebc-11ea-b77f-2e728ce88125`) and
  Cher Ami (`.../cher-ami:687b5a18-e20d-481c-bff3-698d506cd69b`); files under
  https://3d-api.si.edu/content/document/
- Poly Haven: https://polyhaven.com/license ; https://api.polyhaven.com/assets?t=models ;
  https://api.polyhaven.com/files/coast_rocks_02 ; https://github.com/Poly-Haven/Public-API
  (`README.md`, `ToS.md`) ; https://polyhaven.com/a/coast_rocks_02 ; https://polyhaven.com/a/coastal_cliff_04 ;
  https://polyhaven.com/a/pine_tree_01 ; files under https://dl.polyhaven.org/file/ph-assets/Models/
- Three D Scans: https://threedscans.com/info/ ; https://threedscans.com/ferdinandeum-innsbruck/oceanus/ ;
  https://3dprint.com/131873/3d-download-museum-pieces/
- Khronos: https://github.com/KhronosGroup/glTF-Sample-Assets (README, `Models/Sponza`,
  `Models/DragonAttenuation`, `Models/VirtualCity`, `LICENSES/`)
- McGuire archive: https://casual-effects.com/data ;
  https://casual-effects.com/g3d/data10/index/index.js ; per-model `info.js` under
  https://casual-effects.com/g3d/data10/ ; UNC Power Plant (archived):
  https://web.archive.org/web/20250416123746/http://gamma.cs.unc.edu/POWERPLANT/
- NVIDIA ORCA: https://developer.nvidia.com/orca ;
  https://developer.nvidia.com/orca/amazon-lumberyard-bistro ;
  https://developer.nvidia.com/orca/nvidia-emerald-square ; https://developer.nvidia.com/orca/speedtree ;
  https://developer.nvidia.com/orca/beeple-zero-day
- Intel: https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html
  (download IDs 830833, 726650, 726656, 726662, 844047 under `cdrdv2.intel.com/v1/dl/getContent/`)
- Disney Moana Island: https://www.disneyanimation.com/resources/moana-island-scene/ ; license
  https://media.disneyanimation.com/uploads/production/data_set_asset/4/asset/License.txt
- Fab and Epic: https://www.fab.com/eula ; https://www.unrealengine.com/eula/content ;
  https://www.fab.com/sellers/Quixel%20Megascans ;
  https://www.fab.com/listings/0a0f7768-e048-4437-ae93-b9c74d047957 ;
  https://www.fab.com/listings/4898e707-7855-404b-af0e-a505ee690e68 ; Reported:
  https://www.cgchannel.com/2024/10/epic-games-has-made-megascans-free-to-all-but-only-until-the-end-of-2024/
- Blender: https://www.blender.org/download/demo-files/
- Helsinki: https://zenodo.org/records/7599228 (API: https://zenodo.org/api/records/7599228)
- NVIDIA vk_lod_clusters: https://github.com/nvpro-samples/vk_lod_clusters (README,
  `docs/scenes.md`, `docs/camera_paths.md`, `docs/lod_generation.md`)

Measurement

- Karis, "A Deep Dive into Nanite Virtualized Geometry", SIGGRAPH 2021:
  https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf
- Unreal Engine source (EULA-licensed; read, not copied): `Engine/Source/Runtime/Renderer/Private/Nanite/Nanite.cpp`,
  `.../Nanite/NaniteCullRaster.cpp`, `Engine/Shaders/Private/Nanite/NanitePrintStats.usf`,
  `Engine/Shaders/Shared/NaniteDefinitions.h`; branch `ue5-main`, read 2026-10-01.
- Bevy virtual geometry: https://jms55.github.io/posts/2024-06-09-virtual-geometry-bevy-0-14/ ;
  https://jms55.github.io/posts/2024-11-14-virtual-geometry-bevy-0-15/ ;
  https://jms55.github.io/posts/2025-03-27-virtual-geometry-bevy-0-16/
- Kapoulkine: https://zeux.io/2025/09/30/billions-of-triangles-in-minutes/ ;
  https://zeux.io/2026/09/30/billions-of-triangles-redux/ ; meshoptimizer releases and
  `demo/clusterlod.h`: https://github.com/zeux/meshoptimizer
- Haar and Aaltonen, "GPU-Driven Rendering Pipelines", SIGGRAPH 2015:
  http://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf
- Light-system: https://github.com/usestemframework/light-system (`README.md`, `docs/BENCHMARK_PLAN.md`) ;
  Render-Tech-Lab: https://github.com/pasquelin/render-tech-lab
- Yang et al., Virtualized 3D Gaussians: https://arxiv.org/abs/2505.06523 ; Li, Feng and Varshney,
  Continuous Levels of Detail for Light Field Networks: https://arxiv.org/abs/2309.11591 ; Tamm et al.,
  Automatic Testing and Validation of Level of Detail Reductions: https://arxiv.org/abs/2208.12674
- 3D Tiles specification (`specification/README.adoc`) and CesiumJS `Cesium3DTileset.js`:
  https://github.com/CesiumGS/3d-tiles , https://github.com/CesiumGS/cesium ; NVIDIA FLIP:
  https://github.com/NVlabs/flip
- Metal counters: `MTLCounters.h` in the macOS SDK; a local probe of `MTLDevice.counterSets` and
  `supportsCounterSampling` on the recorded development machine.
- Project references: [fetch pattern](../../../xmake/setup.lua),
  [GPU debugging and measurement](../../guides/gpu-debugging.md),
  [GPU visibility guide](../../guides/gpu-visibility.md),
  [screenshot comparison](../../guides/screenshot-comparison.md),
  [temporal comparison tooling](../../../Tools/TemporalCompare/README.md).
