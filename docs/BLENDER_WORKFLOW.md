# Blender to Kronos

Kronos reads glTF 2.0 (`.glb` or `.gltf`). Export from Blender like this:

1. File > Export > glTF 2.0.
2. Format: **glTF Binary (.glb)**. One file keeps the textures with the model.
3. Include: Selected Objects if you only want part of the scene.
4. Transform: leave **+Y Up** on. Kronos uses Y as up.
5. Apply modifiers on, so the mesh you see is the mesh you get.
6. Materials: Export. Kronos uses the base colour, metallic, roughness and normal maps.

Then drag the `.glb` into the Asset Browser or use File > Import. The import report lists anything
that was skipped (cameras, lights and animations are not all supported yet).

Tips
- Keep scale at 1 Blender unit = 1 stud-sized metre; use the Scale box in the importer if it looks tiny.
- Delete hidden objects before exporting, they still count as triangles.
- Check the result with Stats: triangles and draw calls should stay low for a first game.
