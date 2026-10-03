# Historical schema 1 catalog fixtures

These six scene documents, five animation buffers and matching screenshot reference are
byte-for-byte copies from Luminex commit `11264c552068605f2dc7903cdce6efe5785b1a01`, with
schema 1 documents and procedural lab generators. `sha256.json` records each original file hash.

The document reader tests retain schema 1 compatibility and exact canonical checks for all six
documents. The parity tool self-tests use the three referenced scenes to exercise historical
document hashes independently of later catalog edits or accepted reference updates. These
fixtures do not render images, validate current catalog parity or authorize an image rebaseline.
The production parity command continues to use `Tools/Screenshots/reference.json` and the
requested documents.

Keep these historical bytes unchanged when updating the live catalog or reference.
