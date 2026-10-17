# Third-party software in Kronos

What Kronos builds on, with the licence of each. Versions are the ones pinned in
`engine/cmake/Dependencies.cmake`. Checked 17 Oct 2026 from that file and the vendored folders; licence names are
the projects' own published ones. This is an inventory, not legal advice: before a public release, someone should
read each licence text and make sure the notices ship in the download.

## Fetched and built from source

| Project | Version | Licence | Used for |
|---|---|---|---|
| EnTT | 3.16.0 | MIT | entities and components |
| GLM | 1.0.1 | MIT | maths |
| Vulkan-Headers | sdk 1.4.350.1 | Apache-2.0 | Vulkan types |
| volk | sdk 1.4.350.1 | MIT | loads Vulkan |
| Vulkan Memory Allocator | 3.4.0 | MIT | GPU memory |
| Jolt Physics | 5.6.0 | MIT | physics |
| Luau | 0.732 | MIT | scripting |
| ENet | 1.3.18 | MIT | networking |
| Dear ImGui (docking) | 1.92.9 | MIT | Studio and Player UI |
| ImGuiColorTextEdit | pinned commit | MIT | script editor |
| ImGuizmo | pinned commit | MIT | transform gizmos |
| imnodes | pinned commit | MIT | node editors |
| nlohmann/json | 3.11.3 | MIT | JSON |
| tinygltf | 3.0.1 | MIT | glTF import |
| ufbx | 0.23.0 | MIT (or public domain) | FBX import |
| PocketPy | 2.2.0 | MIT | Python scripts |

## Vendored in the repository (`engine/external/vendor`)

| Project | Licence | Used for |
|---|---|---|
| miniaudio | MIT-0 or public domain | audio |
| stb (image, image_write, image_resize2) | MIT or public domain | images |
| tinyexr | BSD-3-Clause | EXR images |

## From the system

| Project | Licence | Notes |
|---|---|---|
| SDL2 | zlib | windows and input |
| libcurl | curl licence (MIT-like) | web requests |
| FFmpeg (libavformat, libavcodec, libavutil, libswscale) | LGPL 2.1+ (GPL if built with GPL parts) | video; linked dynamically from the system, so check what the Windows download bundles |
| libsecret (Linux) | LGPL 2.1+ | stored sign-in |
| Vulkan loader | Apache-2.0 | system library |

## Test data

`engine/tests/fixtures/rbx-test-files` comes from rbx-dom (MIT).

## To do before a public release

- Ship a combined NOTICE file with every licence text in the installer and zip.
- Confirm which FFmpeg build the Windows download bundles and that its licence is followed.
- Re-run this check whenever `Dependencies.cmake` changes (see [SECURITY_PROCESS.md](SECURITY_PROCESS.md)).
