#pragma once

#include <string>
#include <vector>

namespace engine::migration {

enum class AssetKind { Mesh, Texture, Animation, Sound, Unknown };

struct ConversionResult {
    bool succeeded = false;
    std::string outputPath;  // local asset path once converted -- empty on failure
    std::string message;     // human-readable status, always set (success note or failure reason)
};

// Structural stub for docs/ARCHITECTURE.md §7's asset converter: meshes
// (.fbx/.obj/.gltf + Roblox's own mesh format), textures/decals (with
// rbxassetid:// re-mapping), animations (KeyframeSequence -> local clips,
// R15/R6 joint preservation), and sounds (passthrough for common formats).
//
// None of the four convert*() methods do real conversion work yet -- each
// currently only classifies the input and returns a "not yet implemented"
// ConversionResult. What's real here is the *shape* of the pipeline: one
// entry point per asset kind, a uniform result type, and detectKind() as
// the dispatch rule a real implementation (and AssetConverter's future
// callers, like InstanceTreeBuilder resolving a Decal's Texture property)
// would use without caring which concrete converter ran.
class AssetConverter {
public:
    [[nodiscard]] static AssetKind detectKind(const std::string& sourcePath);

    [[nodiscard]] ConversionResult convertMesh(const std::string& sourcePath, const std::string& outputDir);
    [[nodiscard]] ConversionResult convertTexture(const std::string& sourcePath, const std::string& outputDir);
    [[nodiscard]] ConversionResult convertAnimation(const std::string& sourcePath, const std::string& outputDir);
    [[nodiscard]] ConversionResult convertSound(const std::string& sourcePath, const std::string& outputDir);

    // Dispatches to the right convert*() based on detectKind(). This is
    // the entry point InstanceTreeBuilder's follow-on ECS-wiring pass
    // (see its header TODO) would actually call.
    [[nodiscard]] ConversionResult convert(const std::string& sourcePath, const std::string& outputDir);

private:
    [[nodiscard]] static ConversionResult notYetImplemented(const std::string& sourcePath, const char* kindName);
};

} // namespace engine::migration
