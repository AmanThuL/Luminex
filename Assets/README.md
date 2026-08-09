# Assets

Runtime scene assets are not committed to this repository. `xmake setup` downloads Damaged Helmet
from an exact upstream commit and the official 78 MB Crytek Sponza OBJ+PNG archive from Morgan
McGuire's Computer Graphics Archive. It verifies the downloads, preserves the accompanying
provenance and license records, and deterministically converts Sponza into uncompressed core glTF
for the runtime loader. Both the downloaded inputs and generated output stay under gitignored
`Assets/Fetched/`.

These files are external evaluation content, not Apache-2.0 project assets. Damaged Helmet includes
CC-BY-4.0 and CC-BY-NC-4.0 work. The Sponza archive's `info.js` identifies it as CC BY 3.0 and
credits © 2010 Frank Meinl/Crytek. Review
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) before redistributing either scene or publishing
images made from it.
