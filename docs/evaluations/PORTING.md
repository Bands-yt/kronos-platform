# Porting feasibility

Notes only.

## macOS (0554)
Vulkan on macOS runs through MoltenVK (Metal). The renderer needs Vulkan 1.3 features that MoltenVK does not
fully have yet. Jolt, SDL2, ImGui, entt and Luau all build on macOS. Estimate: 2 to 3 weeks to a window, plus
unknown driver work. Needs a Mac to test; not available.

## Android (0916) and iOS (0917)
Same renderer problem plus touch input, app lifecycle (`resumeSteps` in `core/QaPlatformTools.hpp`), store
rules and a mobile Studio question. Android has native Vulkan 1.1 to 1.3 on newer phones. iOS needs MoltenVK.
Verdict: the renderer would need a lower tier (`deviceTier`, `pickShaderVariant`) before either is realistic.

## WebAssembly (0926)
No threads by default, no native Vulkan, and Jolt/Luau would need Emscripten builds. See WEB_DECISIONS.md.
Verdict: a viewer-only web player is more realistic than a web Studio.

## Delta updates (0560)
Ship binary patches (bsdiff or zstd dictionary) between versions. Saves download size, costs build and test
time. Not worth it while the full download is small. Revisit above 200 MB.
