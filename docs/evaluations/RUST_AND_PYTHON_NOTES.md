# Rust and Python notes

## Cargo features (0457)
The `kronos` crate has no optional features yet. When it grows, use: `ui` (panel helpers), `scene` (scene
access), `log`. Plugins should depend on only what they use so the `.so` stays small.

## Editor panel examples (0454)
`templates/rust-plugin` shows a panel with a button and a label. More examples to add: a list view, a tool that
edits the selection, a menu item.

## Hot reload design (0463)
A loaded `.so`/`.dll` cannot be replaced on Windows while open. Design: load a copy from a temp folder, watch
the original, on change unload the copy (call the plugin's shutdown), load a new copy. State must be saved by
the plugin through the host (a key/value area) because the old code is gone. Not built.

## Python hot reload (0447) and debugger hooks (0437)
Studio already reloads Python scripts when they change in play mode. The missing part is keeping module
variables across a reload: save the globals that are plain numbers, strings, lists and dicts, reload, put them
back. A debugger needs PocketPy's trace hook; not exposed yet.
