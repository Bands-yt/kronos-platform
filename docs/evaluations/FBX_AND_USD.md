# FBX and USD evaluation

Status: notes only. Neither format is imported today.

## FBX skeletal animation (0163)
FBX is a closed format; the only supported readers are Autodesk's SDK (licence does not allow shipping it)
and open-source parsers such as ufbx (MIT, a single C file). Plan if we do it:
1. Add ufbx as a vendored dependency (like PocketPy).
2. Read meshes, skin weights, bones and animation stacks into the same structures glTF import uses.
3. Test with a corpus of five small rigs from Blender and Mixamo-style files.
Size: about a week for meshes and skeletons, another week for animation retargeting. Advice: ask creators to
export glTF from Blender first (see BLENDER_WORKFLOW.md); do FBX only if people ask for it.

## USD / USDZ (0164)
USD is large (Pixar's library is a big dependency). A reader for the simple USDZ files that phones and
Apple tools create (a zip with one `.usdc` and textures) is possible but the binary `.usdc` format is complex.
Advice: not before 6.0; glTF covers the same need.
