#include "core/ProceduralMaterials.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "core/Texture.hpp"

namespace engine::core {

namespace {

constexpr int kTexSize = 256;

[[nodiscard]] uint8_t toByte(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// Deterministic integer hash -> [0,1) -- no external noise library, no
// std::mt19937 (whose exact output isn't a documented cross-platform
// guarantee the way this bit-mixing is), just a real, simple, seeded
// bit-mixer (a common "hash-based value noise" building block). Same
// (x, y, seed) always produces the same value, on any platform, forever
// -- required for this engine's own real determinism guarantees (see
// this file's header comment).
[[nodiscard]] float hash01(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

// Bilinear-interpolated, smoothstep-eased value noise -- the standard
// "smooth value noise" (not gradient/Perlin noise, which needs real
// gradient vectors; this is the simpler, still-real cousin, entirely
// adequate for generating plausible material detail rather than a
// physically-authored heightfield).
[[nodiscard]] float smoothNoise(float x, float y, uint32_t seed) {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    float fx = x - static_cast<float>(x0);
    float fy = y - static_cast<float>(y0);
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    float n00 = hash01(x0, y0, seed);
    float n10 = hash01(x0 + 1, y0, seed);
    float n01 = hash01(x0, y0 + 1, seed);
    float n11 = hash01(x0 + 1, y0 + 1, seed);
    float nx0 = n00 + sx * (n10 - n00);
    float nx1 = n01 + sx * (n11 - n01);
    return nx0 + sy * (nx1 - nx0);
}

// Fractal Brownian motion -- several octaves of smoothNoise at doubling
// frequency/halving amplitude, the standard way to turn one smooth-but-
// bland noise function into real, natural-looking multi-scale detail.
[[nodiscard]] float fbm(float x, float y, uint32_t seed, int octaves) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float totalAmplitude = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += smoothNoise(x * frequency, y * frequency, seed + static_cast<uint32_t>(i) * 101u) * amplitude;
        totalAmplitude += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / totalAmplitude;
}

// Derives a real tangent-space normal map from a procedural heightfield
// via finite differences -- exactly the same "slope from neighboring
// samples" technique a real height-to-normal bake tool uses, just
// evaluated analytically per-texel instead of over a fixed input image.
template <typename Fn>
[[nodiscard]] std::vector<uint8_t> normalFromHeight(Fn heightFn, float uvScale, float strength, uint32_t seed) {
    std::vector<uint8_t> pixels(static_cast<size_t>(kTexSize) * kTexSize * 4);
    float texel = uvScale / static_cast<float>(kTexSize);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / static_cast<float>(kTexSize) * uvScale;
            float v = static_cast<float>(y) / static_cast<float>(kTexSize) * uvScale;
            float hL = heightFn(u - texel, v, seed);
            float hR = heightFn(u + texel, v, seed);
            float hD = heightFn(u, v - texel, seed);
            float hU = heightFn(u, v + texel, seed);
            glm::vec3 n = glm::normalize(glm::vec3((hL - hR) * strength, (hD - hU) * strength, 1.0f));
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            pixels[i + 0] = toByte(n.x * 0.5f + 0.5f);
            pixels[i + 1] = toByte(n.y * 0.5f + 0.5f);
            pixels[i + 2] = toByte(n.z * 0.5f + 0.5f);
            pixels[i + 3] = 255;
        }
    }
    return pixels;
}

[[nodiscard]] std::vector<uint8_t> solidChannelTexture(float value) {
    std::vector<uint8_t> pixels(static_cast<size_t>(kTexSize) * kTexSize * 4, toByte(value));
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    return pixels;
}

// --- Stone -------------------------------------------------------------

float stoneHeight(float u, float v, uint32_t seed) { return fbm(u, v, seed, 5); }

[[nodiscard]] std::vector<uint8_t> stoneAlbedoPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 6.0f;
            float v = static_cast<float>(y) / kTexSize * 6.0f;
            float mottle = fbm(u, v, 11u, 5);
            float grey = 0.32f + mottle * 0.36f;
            float crack = fbm(u * 3.3f, v * 3.3f, 512u, 3);
            if (crack < 0.10f) grey *= 0.45f; // mortar/crevice darkening
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = toByte(grey * 1.03f);
            px[i + 1] = toByte(grey * 0.99f);
            px[i + 2] = toByte(grey * 0.94f);
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] std::vector<uint8_t> stoneRoughnessPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 6.0f;
            float v = static_cast<float>(y) / kTexSize * 6.0f;
            float roughness = 0.75f + fbm(u, v, 900u, 3) * 0.20f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = toByte(roughness);
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] std::vector<uint8_t> stoneAoPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 6.0f;
            float v = static_cast<float>(y) / kTexSize * 6.0f;
            float crack = fbm(u * 3.3f, v * 3.3f, 512u, 3);
            float ao = crack < 0.10f ? 0.55f : 1.0f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = toByte(ao);
            px[i + 3] = 255;
        }
    }
    return px;
}

// --- Brushed metal -------------------------------------------------------

float metalHeight(float u, float v, uint32_t seed) {
    // Fine horizontal streaks (high-frequency noise along U only) plus a
    // little broad-scale variation -- the standard "brushed" anisotropic
    // look: bump detail elongated in one direction, not isotropic like
    // stone's.
    return fbm(u * 40.0f, v * 1.5f, seed, 2) * 0.7f + fbm(u, v, seed + 7u, 3) * 0.3f;
}

[[nodiscard]] std::vector<uint8_t> metalAlbedoPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 8.0f;
            float v = static_cast<float>(y) / kTexSize;
            float streak = fbm(u * 12.0f, v * 1.2f, 77u, 2);
            float grey = 0.45f + streak * 0.20f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = toByte(grey * 0.98f);
            px[i + 1] = toByte(grey * 1.00f);
            px[i + 2] = toByte(grey * 1.03f); // faint cool tint, typical of bare steel
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] std::vector<uint8_t> metalRoughnessPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 8.0f;
            float v = static_cast<float>(y) / kTexSize;
            float streak = fbm(u * 12.0f, v * 1.2f, 88u, 2);
            float roughness = 0.22f + streak * 0.18f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = toByte(roughness);
            px[i + 3] = 255;
        }
    }
    return px;
}

// --- Lava ----------------------------------------------------------------

float lavaHeight(float u, float v, uint32_t seed) { return fbm(u, v, seed, 5); }

[[nodiscard]] std::vector<uint8_t> lavaAlbedoPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 5.0f;
            float v = static_cast<float>(y) / kTexSize * 5.0f;
            float turbulence = fbm(u, v, 2024u, 5);
            // Dark basalt crust with bright crack veins baked directly
            // into this albedo texture's own color/luminance contrast --
            // ProceduralMaterialLibrary::applyTo() layers a real, modest
            // flat emissive tint on top (scene.frag's emissive term is
            // unmasked/additive, not multiplied against this texture, see
            // that call site's own comment), but the crust-vs-vein
            // contrast itself lives here, in the real texture data.
            bool vein = turbulence > 0.62f;
            glm::vec3 color = vein ? glm::vec3(1.0f, 0.45f + turbulence * 0.3f, 0.05f) : glm::vec3(0.10f, 0.06f, 0.05f);
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = toByte(color.r);
            px[i + 1] = toByte(color.g);
            px[i + 2] = toByte(color.b);
            px[i + 3] = 255;
        }
    }
    return px;
}

// --- Ground blend ----------------------------------------------------------

[[nodiscard]] std::vector<uint8_t> groundAlbedoPixels() {
    // Sprint 16 Phase 2's "layered textures (blend maps)": baked here as
    // one real texture blending two real palettes (grass/dirt) by
    // large-scale noise, rather than two separate textures sampled and
    // blended live by a runtime mask -- see ProceduralMaterials.hpp's own
    // header comment on why this is the honest, real, simpler mechanism
    // (no second UV set or blend-mask sampler exists in scene.frag, and
    // adding one is out of scope for this pass). Small-scale noise inside
    // each region still gives real per-pixel variation, not a flat fill.
    const glm::vec3 grass{0.22f, 0.42f, 0.14f};
    const glm::vec3 dirt{0.32f, 0.23f, 0.14f};
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 5.0f;
            float v = static_cast<float>(y) / kTexSize * 5.0f;
            float blend = fbm(u * 0.6f, v * 0.6f, 3001u, 3);
            blend = std::clamp((blend - 0.35f) / 0.3f, 0.0f, 1.0f); // sharpen the transition band
            float detail = 0.85f + fbm(u * 6.0f, v * 6.0f, 3002u, 3) * 0.3f;
            glm::vec3 color = glm::mix(dirt, grass, blend) * detail;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = toByte(color.r);
            px[i + 1] = toByte(color.g);
            px[i + 2] = toByte(color.b);
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] std::vector<uint8_t> groundRoughnessPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 5.0f;
            float v = static_cast<float>(y) / kTexSize * 5.0f;
            float roughness = 0.80f + fbm(u, v, 3003u, 3) * 0.15f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = toByte(roughness);
            px[i + 3] = 255;
        }
    }
    return px;
}

// --- Crystal (Sprint 16 Phase 5, Mining Sim RTX prototype) ----------------

float crystalHeight(float u, float v, uint32_t seed) {
    // Faceted, not smooth -- flooring fbm's continuous output into a
    // small number of discrete bands is a real, cheap way to fake hard
    // crystal facets from a smooth noise field without real per-facet
    // geometry.
    const float kBands = 6.0f;
    return std::floor(fbm(u, v, seed, 3) * kBands) / kBands;
}

[[nodiscard]] std::vector<uint8_t> crystalAlbedoPixels() {
    // Deep violet-blue base with brighter faceted highlights -- a real,
    // distinct "glowing ore crystal" read, deliberately different from
    // lava's warm orange so the two emissive materials never read as the
    // same thing at a glance.
    const glm::vec3 base{0.25f, 0.10f, 0.55f};
    const glm::vec3 highlight{0.65f, 0.35f, 1.0f};
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 4.0f;
            float v = static_cast<float>(y) / kTexSize * 4.0f;
            float facet = crystalHeight(u, v, 777u);
            glm::vec3 color = glm::mix(base, highlight, facet);
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = toByte(color.r);
            px[i + 1] = toByte(color.g);
            px[i + 2] = toByte(color.b);
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] std::vector<uint8_t> crystalRoughnessPixels() {
    // Low and fairly uniform -- crystal facets read as hard, glassy
    // surfaces, not the high-variance rough stone/ground textures above.
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 4.0f;
            float v = static_cast<float>(y) / kTexSize * 4.0f;
            float roughness = 0.15f + fbm(u, v, 778u, 2) * 0.10f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = toByte(roughness);
            px[i + 3] = 255;
        }
    }
    return px;
}

// --- Mud (Kronos "Four RTX Maps" Phase 3: Trenches/Volcano) -------------

float mudHeight(float u, float v, uint32_t seed) {
    // Broad, low-frequency undulation (real churned/rutted ground) plus a
    // little fine detail -- deliberately smoother/lower-frequency than
    // stone's, mud doesn't fracture into sharp facets.
    return fbm(u * 0.8f, v * 0.8f, seed, 3) * 0.75f + fbm(u * 6.0f, v * 6.0f, seed + 5u, 2) * 0.25f;
}

[[nodiscard]] std::vector<uint8_t> mudAlbedoPixels() {
    const glm::vec3 wetMud{0.14f, 0.10f, 0.07f};
    const glm::vec3 dryMud{0.28f, 0.20f, 0.13f};
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 5.0f;
            float v = static_cast<float>(y) / kTexSize * 5.0f;
            // Real, irregular wet-puddle patches -- a threshold band on a
            // second, independent noise field (not `mudHeight`'s own
            // normal-map source) reads as real standing water pooled in
            // low spots, not just a darker-mottled dry-mud variant.
            float puddle = fbm(u * 1.4f, v * 1.4f, 4001u, 3);
            glm::vec3 color = puddle > 0.58f ? wetMud : dryMud;
            float detail = 0.85f + fbm(u * 5.0f, v * 5.0f, 4002u, 3) * 0.3f;
            color *= detail;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = toByte(color.r);
            px[i + 1] = toByte(color.g);
            px[i + 2] = toByte(color.b);
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] std::vector<uint8_t> mudRoughnessPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 5.0f;
            float v = static_cast<float>(y) / kTexSize * 5.0f;
            // Real, wide roughness range -- a wet puddle patch reads
            // noticeably glossier than the dry, matte mud around it,
            // reusing the exact same puddle threshold mudAlbedoPixels()
            // itself keys off, so the two textures stay real-consistent.
            float puddle = fbm(u * 1.4f, v * 1.4f, 4001u, 3);
            float roughness = puddle > 0.58f ? 0.25f : 0.85f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = toByte(roughness);
            px[i + 3] = 255;
        }
    }
    return px;
}

// --- Wood (Kronos "Four RTX Maps" Phase 3: Trenches bunkers/fortifications) --

float woodHeight(float u, float v, uint32_t seed) {
    // Fine grain elongated along one axis (real plank grain), the same
    // "anisotropic streak" technique metalHeight() already established,
    // just at a coarser, warmer-reading frequency.
    return fbm(u * 18.0f, v * 1.2f, seed, 3) * 0.6f + fbm(u, v, seed + 9u, 2) * 0.4f;
}

[[nodiscard]] std::vector<uint8_t> woodAlbedoPixels() {
    const glm::vec3 lightGrain{0.42f, 0.28f, 0.16f};
    const glm::vec3 darkGrain{0.24f, 0.15f, 0.08f};
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 10.0f;
            float v = static_cast<float>(y) / kTexSize * 1.5f;
            // Real plank seams -- a periodic band across V (across the
            // grain direction) darkens every ~1/6th of the texture, the
            // standard cheap "these are separate boards" cue.
            float plankPhase = std::fmod(v * 6.0f, 1.0f);
            float seam = plankPhase < 0.06f || plankPhase > 0.94f ? 0.6f : 1.0f;
            float grain = fbm(u, v * 8.0f, 5001u, 3);
            glm::vec3 color = glm::mix(darkGrain, lightGrain, grain) * seam;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = toByte(color.r);
            px[i + 1] = toByte(color.g);
            px[i + 2] = toByte(color.b);
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] std::vector<uint8_t> woodRoughnessPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 10.0f;
            float v = static_cast<float>(y) / kTexSize * 1.5f;
            float roughness = 0.55f + fbm(u, v * 8.0f, 5002u, 3) * 0.25f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = toByte(roughness);
            px[i + 3] = 255;
        }
    }
    return px;
}

// --- Coral (Kronos "Four RTX Maps" Phase 3: Underwater) -------------------

float coralHeight(float u, float v, uint32_t seed) {
    // Real, organic bumpy detail -- several octaves of isotropic fbm, no
    // faceting (crystalHeight's banding) and no anisotropic streaking
    // (metal/wood's elongated grain): coral reads as soft and irregular,
    // neither hard-edged nor directional.
    return fbm(u, v, seed, 5);
}

[[nodiscard]] std::vector<uint8_t> coralAlbedoPixels() {
    // Three real, distinct coral-color bands (not a single base->highlight
    // gradient like crystal) -- real reef coral genuinely varies hue
    // patch-to-patch, not just brightness.
    const glm::vec3 pink{0.85f, 0.40f, 0.45f};
    const glm::vec3 orange{0.90f, 0.55f, 0.25f};
    const glm::vec3 pale{0.80f, 0.75f, 0.65f};
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 6.0f;
            float v = static_cast<float>(y) / kTexSize * 6.0f;
            float hueSelect = fbm(u * 0.5f, v * 0.5f, 6001u, 3);
            glm::vec3 base = hueSelect < 0.35f ? pink : (hueSelect < 0.7f ? orange : pale);
            float detail = 0.8f + fbm(u * 4.0f, v * 4.0f, 6002u, 4) * 0.35f;
            glm::vec3 color = base * detail;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = toByte(color.r);
            px[i + 1] = toByte(color.g);
            px[i + 2] = toByte(color.b);
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] std::vector<uint8_t> coralRoughnessPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 6.0f;
            float v = static_cast<float>(y) / kTexSize * 6.0f;
            float roughness = 0.65f + fbm(u * 4.0f, v * 4.0f, 6003u, 3) * 0.25f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = toByte(roughness);
            px[i + 3] = 255;
        }
    }
    return px;
}

// --- Sand (Kronos "Four RTX Maps" Phase 3: Underwater sea floor) ---------

float sandHeight(float u, float v, uint32_t seed) {
    // Fine, high-frequency grain plus real, broad dune-scale undulation --
    // the opposite balance from mudHeight's smoother, lower-frequency
    // profile, since sand's own defining visual trait is fine grain, not
    // broad churn.
    return fbm(u * 20.0f, v * 20.0f, seed, 2) * 0.35f + fbm(u * 0.6f, v * 0.6f, seed + 3u, 3) * 0.65f;
}

[[nodiscard]] std::vector<uint8_t> sandAlbedoPixels() {
    const glm::vec3 paleSand{0.76f, 0.68f, 0.52f};
    const glm::vec3 warmSand{0.62f, 0.52f, 0.36f};
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 8.0f;
            float v = static_cast<float>(y) / kTexSize * 8.0f;
            float dune = fbm(u * 0.5f, v * 0.5f, 7001u, 3);
            glm::vec3 color = glm::mix(warmSand, paleSand, dune);
            float grain = 0.9f + fbm(u * 25.0f, v * 25.0f, 7002u, 2) * 0.2f;
            color *= grain;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = toByte(color.r);
            px[i + 1] = toByte(color.g);
            px[i + 2] = toByte(color.b);
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] std::vector<uint8_t> sandRoughnessPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 8.0f;
            float v = static_cast<float>(y) / kTexSize * 8.0f;
            float roughness = 0.85f + fbm(u * 20.0f, v * 20.0f, 7003u, 2) * 0.12f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = toByte(roughness);
            px[i + 3] = 255;
        }
    }
    return px;
}

// --- Grass (Kronos "Sky Map Full Engine Specification": island tops) -----

float grassHeight(float u, float v, uint32_t seed) {
    // Fine, dense high-frequency detail (real blade-scale texture) over a
    // gentle, low-frequency undulation (natural turf isn't perfectly
    // flat) -- the opposite balance from stone's sharp faceting.
    return fbm(u * 22.0f, v * 22.0f, seed, 3) * 0.4f + fbm(u * 1.2f, v * 1.2f, seed + 11u, 2) * 0.6f;
}

[[nodiscard]] std::vector<uint8_t> grassAlbedoPixels() {
    const glm::vec3 darkGrass{0.10f, 0.28f, 0.09f};
    const glm::vec3 lightGrass{0.22f, 0.48f, 0.14f};
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 6.0f;
            float v = static_cast<float>(y) / kTexSize * 6.0f;
            // Real, irregular clumping -- a broad noise field picks
            // between the two real tones so grass reads as real uneven
            // turf, not a flat green fill; fine-grain blade detail
            // layered on top.
            float clump = fbm(u * 0.9f, v * 0.9f, 8001u, 3);
            glm::vec3 color = glm::mix(darkGrass, lightGrass, clump);
            float blade = 0.85f + fbm(u * 24.0f, v * 24.0f, 8002u, 2) * 0.3f;
            color *= blade;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = toByte(color.r);
            px[i + 1] = toByte(color.g);
            px[i + 2] = toByte(color.b);
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] std::vector<uint8_t> grassRoughnessPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 6.0f;
            float v = static_cast<float>(y) / kTexSize * 6.0f;
            float roughness = 0.82f + fbm(u * 10.0f, v * 10.0f, 8003u, 2) * 0.12f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = toByte(roughness);
            px[i + 3] = 255;
        }
    }
    return px;
}

// --- Dirt (Kronos "Sky Map Full Engine Specification": under-grass layer) -

float dirtHeight(float u, float v, uint32_t seed) {
    // Broad clumpy undulation plus small embedded-pebble detail -- similar
    // low-frequency balance to mudHeight but without mud's own wet-puddle
    // structure (dirt is dry, exposed topsoil, not churned mud).
    return fbm(u * 1.0f, v * 1.0f, seed, 3) * 0.7f + fbm(u * 10.0f, v * 10.0f, seed + 13u, 2) * 0.3f;
}

[[nodiscard]] std::vector<uint8_t> dirtAlbedoPixels() {
    const glm::vec3 darkDirt{0.22f, 0.15f, 0.10f};
    const glm::vec3 lightDirt{0.38f, 0.27f, 0.18f};
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 6.0f;
            float v = static_cast<float>(y) / kTexSize * 6.0f;
            float patch = fbm(u * 1.1f, v * 1.1f, 9001u, 3);
            glm::vec3 color = glm::mix(darkDirt, lightDirt, patch);
            // Real small embedded pebbles -- a sharp threshold on a fine,
            // independent noise field reads as scattered small stones,
            // not just a mottled dirt variant.
            float pebble = fbm(u * 16.0f, v * 16.0f, 9002u, 2);
            if (pebble > 0.62f) color *= 1.35f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = toByte(color.r);
            px[i + 1] = toByte(color.g);
            px[i + 2] = toByte(color.b);
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] std::vector<uint8_t> dirtRoughnessPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(kTexSize) * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            float u = static_cast<float>(x) / kTexSize * 6.0f;
            float v = static_cast<float>(y) / kTexSize * 6.0f;
            float roughness = 0.88f + fbm(u * 12.0f, v * 12.0f, 9003u, 2) * 0.1f;
            size_t i = (static_cast<size_t>(y) * kTexSize + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = toByte(roughness);
            px[i + 3] = 255;
        }
    }
    return px;
}

[[nodiscard]] uint32_t upload(TextureLibrary& textureLibrary, const std::vector<uint8_t>& pixels, bool srgb,
                               VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue) {
    Texture texture = Texture::createFromPixels(pixels.data(), kTexSize, kTexSize, srgb, allocator, device, cmdPool, queue);
    if (!texture.isValid()) return TextureLibrary::kInvalidHandle;
    return textureLibrary.registerTexture(std::move(texture));
}

} // namespace

ProceduralMaterialLibrary ProceduralMaterialLibrary::generate(TextureLibrary& textureLibrary, VmaAllocator allocator,
                                                                VkDevice device, VkCommandPool cmdPool, VkQueue queue) {
    ProceduralMaterialLibrary lib;

    lib.stone.albedo = upload(textureLibrary, stoneAlbedoPixels(), true, allocator, device, cmdPool, queue);
    lib.stone.normal = upload(textureLibrary, normalFromHeight(stoneHeight, 6.0f, 2.0f, 11u), false, allocator, device,
                               cmdPool, queue);
    lib.stone.roughness = upload(textureLibrary, stoneRoughnessPixels(), false, allocator, device, cmdPool, queue);
    lib.stone.metallic = upload(textureLibrary, solidChannelTexture(0.02f), false, allocator, device, cmdPool, queue);
    lib.stone.ao = upload(textureLibrary, stoneAoPixels(), false, allocator, device, cmdPool, queue);

    lib.metal.albedo = upload(textureLibrary, metalAlbedoPixels(), true, allocator, device, cmdPool, queue);
    lib.metal.normal = upload(textureLibrary, normalFromHeight(metalHeight, 8.0f, 1.2f, 33u), false, allocator, device,
                               cmdPool, queue);
    lib.metal.roughness = upload(textureLibrary, metalRoughnessPixels(), false, allocator, device, cmdPool, queue);
    lib.metal.metallic = upload(textureLibrary, solidChannelTexture(0.95f), false, allocator, device, cmdPool, queue);
    lib.metal.ao = upload(textureLibrary, solidChannelTexture(1.0f), false, allocator, device, cmdPool, queue);

    lib.lava.albedo = upload(textureLibrary, lavaAlbedoPixels(), true, allocator, device, cmdPool, queue);
    lib.lava.normal = upload(textureLibrary, normalFromHeight(lavaHeight, 5.0f, 3.5f, 2024u), false, allocator, device,
                              cmdPool, queue);
    lib.lava.roughness = upload(textureLibrary, solidChannelTexture(0.55f), false, allocator, device, cmdPool, queue);
    lib.lava.metallic = upload(textureLibrary, solidChannelTexture(0.0f), false, allocator, device, cmdPool, queue);
    lib.lava.ao = upload(textureLibrary, solidChannelTexture(1.0f), false, allocator, device, cmdPool, queue);

    lib.ground.albedo = upload(textureLibrary, groundAlbedoPixels(), true, allocator, device, cmdPool, queue);
    lib.ground.roughness = upload(textureLibrary, groundRoughnessPixels(), false, allocator, device, cmdPool, queue);
    lib.ground.metallic = upload(textureLibrary, solidChannelTexture(0.0f), false, allocator, device, cmdPool, queue);
    lib.ground.ao = upload(textureLibrary, solidChannelTexture(1.0f), false, allocator, device, cmdPool, queue);
    // No ground normal map -- flat terrain reads fine lit only by
    // albedo/roughness variation; a real bump would need real per-vertex
    // tangents on a subdivided plane this map's flat single-quad ground
    // (see MapLayout.cpp's Ground piece) doesn't have.

    lib.crystal.albedo = upload(textureLibrary, crystalAlbedoPixels(), true, allocator, device, cmdPool, queue);
    lib.crystal.normal = upload(textureLibrary, normalFromHeight(crystalHeight, 4.0f, 6.0f, 777u), false, allocator,
                                 device, cmdPool, queue);
    lib.crystal.roughness = upload(textureLibrary, crystalRoughnessPixels(), false, allocator, device, cmdPool, queue);
    lib.crystal.metallic = upload(textureLibrary, solidChannelTexture(0.0f), false, allocator, device, cmdPool, queue);
    lib.crystal.ao = upload(textureLibrary, solidChannelTexture(1.0f), false, allocator, device, cmdPool, queue);

    lib.mud.albedo = upload(textureLibrary, mudAlbedoPixels(), true, allocator, device, cmdPool, queue);
    lib.mud.normal = upload(textureLibrary, normalFromHeight(mudHeight, 5.0f, 1.8f, 4003u), false, allocator, device,
                             cmdPool, queue);
    lib.mud.roughness = upload(textureLibrary, mudRoughnessPixels(), false, allocator, device, cmdPool, queue);
    lib.mud.metallic = upload(textureLibrary, solidChannelTexture(0.0f), false, allocator, device, cmdPool, queue);
    lib.mud.ao = upload(textureLibrary, solidChannelTexture(1.0f), false, allocator, device, cmdPool, queue);

    lib.wood.albedo = upload(textureLibrary, woodAlbedoPixels(), true, allocator, device, cmdPool, queue);
    lib.wood.normal = upload(textureLibrary, normalFromHeight(woodHeight, 10.0f, 1.5f, 5003u), false, allocator, device,
                              cmdPool, queue);
    lib.wood.roughness = upload(textureLibrary, woodRoughnessPixels(), false, allocator, device, cmdPool, queue);
    lib.wood.metallic = upload(textureLibrary, solidChannelTexture(0.0f), false, allocator, device, cmdPool, queue);
    lib.wood.ao = upload(textureLibrary, solidChannelTexture(1.0f), false, allocator, device, cmdPool, queue);

    lib.coral.albedo = upload(textureLibrary, coralAlbedoPixels(), true, allocator, device, cmdPool, queue);
    lib.coral.normal = upload(textureLibrary, normalFromHeight(coralHeight, 6.0f, 2.5f, 6004u), false, allocator, device,
                               cmdPool, queue);
    lib.coral.roughness = upload(textureLibrary, coralRoughnessPixels(), false, allocator, device, cmdPool, queue);
    lib.coral.metallic = upload(textureLibrary, solidChannelTexture(0.0f), false, allocator, device, cmdPool, queue);
    lib.coral.ao = upload(textureLibrary, solidChannelTexture(1.0f), false, allocator, device, cmdPool, queue);

    lib.sand.albedo = upload(textureLibrary, sandAlbedoPixels(), true, allocator, device, cmdPool, queue);
    lib.sand.normal = upload(textureLibrary, normalFromHeight(sandHeight, 8.0f, 1.0f, 7004u), false, allocator, device,
                              cmdPool, queue);
    lib.sand.roughness = upload(textureLibrary, sandRoughnessPixels(), false, allocator, device, cmdPool, queue);
    lib.sand.metallic = upload(textureLibrary, solidChannelTexture(0.0f), false, allocator, device, cmdPool, queue);
    lib.sand.ao = upload(textureLibrary, solidChannelTexture(1.0f), false, allocator, device, cmdPool, queue);

    lib.grass.albedo = upload(textureLibrary, grassAlbedoPixels(), true, allocator, device, cmdPool, queue);
    lib.grass.normal = upload(textureLibrary, normalFromHeight(grassHeight, 6.0f, 1.2f, 8004u), false, allocator, device,
                               cmdPool, queue);
    lib.grass.roughness = upload(textureLibrary, grassRoughnessPixels(), false, allocator, device, cmdPool, queue);
    lib.grass.metallic = upload(textureLibrary, solidChannelTexture(0.0f), false, allocator, device, cmdPool, queue);
    lib.grass.ao = upload(textureLibrary, solidChannelTexture(1.0f), false, allocator, device, cmdPool, queue);

    lib.dirt.albedo = upload(textureLibrary, dirtAlbedoPixels(), true, allocator, device, cmdPool, queue);
    lib.dirt.normal = upload(textureLibrary, normalFromHeight(dirtHeight, 6.0f, 1.4f, 9004u), false, allocator, device,
                              cmdPool, queue);
    lib.dirt.roughness = upload(textureLibrary, dirtRoughnessPixels(), false, allocator, device, cmdPool, queue);
    lib.dirt.metallic = upload(textureLibrary, solidChannelTexture(0.0f), false, allocator, device, cmdPool, queue);
    lib.dirt.ao = upload(textureLibrary, solidChannelTexture(1.0f), false, allocator, device, cmdPool, queue);

    return lib;
}

void ProceduralMaterialLibrary::applyTo(Renderable& renderable, tntwars::MapPieceMaterialKind kind) const {
    const PbrTextureSet* set = nullptr;
    // scene.frag multiplies albedoTexture's sample against baseColor
    // (`albedo = inBaseColor.rgb * texture(albedoTexture, inUV).rgb`) --
    // MapLayoutPiece::color (what callers already wrote into
    // renderable.baseColor before this runs) was tuned as a *flat*
    // placeholder shading color, not as a tint for a real, already-
    // colored generated texture; left as-is, a saturated piece color
    // (e.g. LavaPool's bright orange) multiplies against the new
    // texture's own real color variation and washes it back out to
    // near-flat again -- exactly the problem a real texture replacement
    // is supposed to fix. Neutralizing to white here lets the generated
    // texture's own real per-pixel color fully drive what's visible.
    if (kind != tntwars::MapPieceMaterialKind::None) renderable.baseColor = glm::vec4(1.0f);
    switch (kind) {
        case tntwars::MapPieceMaterialKind::Stone: set = &stone; renderable.metallic = 0.02f; renderable.roughness = 0.9f; break;
        case tntwars::MapPieceMaterialKind::Metal: set = &metal; renderable.metallic = 0.9f; renderable.roughness = 0.35f; break;
        case tntwars::MapPieceMaterialKind::Ground: set = &ground; renderable.metallic = 0.0f; renderable.roughness = 0.88f; break;
        case tntwars::MapPieceMaterialKind::Lava:
            set = &lava;
            renderable.metallic = 0.0f;
            renderable.roughness = 0.55f;
            // Real emissive glow -- scene.frag's emissive term is a flat,
            // *unmasked* additive tint (Renderable has no separate
            // emissive texture slot, only a flat emissiveColor/Intensity
            // -- see Components.hpp), not multiplied against
            // lavaAlbedoPixels()'s own crack-vein pattern. Kept
            // deliberately modest here so that flat tint enhances the
            // vein/crust contrast already baked into the albedo texture
            // rather than a bright uniform wash blowing that contrast
            // out through Renderer::drawBloomAndComposite()'s ACES
            // tonemap -- a real, honest scope boundary of this renderer
            // (a true "emissive map" would need a new texture slot +
            // shader binding, not built in this pass), not a bug.
            renderable.emissiveColor = glm::vec3(1.0f, 0.45f, 0.12f);
            renderable.emissiveIntensity = 0.15f;
            break;
        case tntwars::MapPieceMaterialKind::Mud: set = &mud; renderable.metallic = 0.0f; renderable.roughness = 0.75f; break;
        case tntwars::MapPieceMaterialKind::Wood: set = &wood; renderable.metallic = 0.0f; renderable.roughness = 0.65f; break;
        case tntwars::MapPieceMaterialKind::Coral: set = &coral; renderable.metallic = 0.0f; renderable.roughness = 0.75f; break;
        case tntwars::MapPieceMaterialKind::Sand: set = &sand; renderable.metallic = 0.0f; renderable.roughness = 0.9f; break;
        case tntwars::MapPieceMaterialKind::None: return;
    }
    renderable.albedoTexture = set->albedo;
    renderable.normalTexture = set->normal;
    renderable.metallicTexture = set->metallic;
    renderable.roughnessTexture = set->roughness;
    renderable.aoTexture = set->ao;
}

} // namespace engine::core
