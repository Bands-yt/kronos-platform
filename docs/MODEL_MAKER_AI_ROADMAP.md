# Kronos 3D Model Maker & AI Model Generator — Roadmap

Saved verbatim from the creator's own planning notes. **Not started** —
this is forward-looking scope beyond the current Studio-declutter/
starter-plugins/house-demo work in progress. Recorded here so it isn't
lost, and to be picked up in the order below once that work lands.

## 1. Real 3D Model Maker (Blender-style, inside Kronos Studio)

### Phase 1 — Core Mesh Editing
- Basic primitives: Cube, Sphere, Cylinder, Wedge, Plane
- Core tools: Move, Rotate, Scale, Snap to grid, Duplicate, Delete
- Mesh API: `Mesh::createCube()`, `Mesh::createSphere()`,
  `Mesh::createCylinder()`, `Mesh::createWedge()`, `Mesh::createPlane()`

### Phase 2 — Vertex/Face Editing
- Selection modes: Vertex, Edge, Face
- Edit operations: Extrude, Subdivide, Merge vertices, Bevel edges,
  Inset faces
- UI: "Modeling Mode" toggle in Studio, gizmos for move/rotate/scale,
  sidebar for operations

### Phase 3 — Materials & UV
- Material assignment: per-mesh, per-face
- UV tools (basic): auto unwrap, simple planar/cube projection
- Preview: material preview sphere, viewport shading modes
  (Wireframe / Lit)

### Phase 4 — Export/Import
- Export formats: `.obj`, `.fbx` (later), Kronos native mesh format
- Import: load external `.obj`, convert to Kronos mesh

## 2. AI Model Generator (later release)

### Phase A — Offline AI pipeline (cheap & safe)
- Use external tools/models (not bundled): text → 3D via external
  service or local script, output as `.obj`/`.fbx`/Kronos mesh
- Kronos integration: "Import AI Model" button in Studio, user points
  to the generated file — no live AI inside Kronos yet, keeps cost low

### Phase B — Optional online AI integration (expensive)
- Hosted API: text prompt → 3D asset, rate-limited per user, possibly
  a paid tier
- Studio UI: "Generate 3D Model (AI)" panel, prompt box + "Generate"
  button, preview + import into scene

### Phase C — Local AI (future, heavy)
- Local model (GPU-heavy), advanced users only — not for early
  Alpha/Beta

## 3. Recommended order
1. Block Builder + mesh primitives (in progress — see current session's
   Block Builder plugin work, which already builds `Mesh::createCylinder()`
   and a wedge generator as part of that tool)
2. Modeling Mode (vertex/edge/face editing)
3. Material & UV basics
4. Export/Import pipeline
5. Offline AI integration (external tools, manual import)
6. Optional online AI generator (later, if worth the cost)
