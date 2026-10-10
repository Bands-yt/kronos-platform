#include "migration/RbxBinaryReader.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

#include <glm/glm.hpp>

// Format reference: https://dom.rojo.space/binary.html
namespace engine::migration {
namespace {

constexpr char kMagic[] = "<roblox!";
constexpr unsigned char kSignature[] = {0x89, 0xFF, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr size_t kMaxChunkBytes = 256u * 1024u * 1024u;

class Reader {
public:
    Reader(const unsigned char* data, size_t size) : data_(data), size_(size) {}

    bool ok() const { return ok_; }
    bool atEnd() const { return pos_ >= size_; }
    size_t left() const { return ok_ ? size_ - pos_ : 0; }

    const unsigned char* take(size_t n) {
        if (!ok_ || n > size_ - pos_) {
            ok_ = false;
            return nullptr;
        }
        const unsigned char* at = data_ + pos_;
        pos_ += n;
        return at;
    }
    uint8_t u8() {
        const unsigned char* b = take(1);
        return b != nullptr ? b[0] : 0;
    }
    uint16_t u16() {
        const unsigned char* b = take(2);
        return b != nullptr ? static_cast<uint16_t>(b[0] | b[1] << 8) : 0;
    }
    uint32_t u32() {
        const unsigned char* b = take(4);
        return b != nullptr ? static_cast<uint32_t>(b[0]) | static_cast<uint32_t>(b[1]) << 8 |
                                  static_cast<uint32_t>(b[2]) << 16 | static_cast<uint32_t>(b[3]) << 24
                            : 0;
    }
    float f32() {
        const uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    double f64() {
        const unsigned char* b = take(8);
        if (b == nullptr) return 0.0;
        uint64_t bits = 0;
        for (int i = 7; i >= 0; --i) bits = bits << 8 | b[i];
        double v;
        std::memcpy(&v, &bits, 8);
        return v;
    }
    std::string str() {
        const uint32_t n = u32();
        const unsigned char* b = take(n);
        return b != nullptr ? std::string(reinterpret_cast<const char*>(b), n) : std::string();
    }
    // `count` values of `width` bytes stored byte-plane by byte-plane, read big-endian.
    std::vector<uint64_t> interleaved(size_t count, size_t width) {
        std::vector<uint64_t> out(count, 0);
        if (count > left() / width + 1) {
            ok_ = false;
            return {};
        }
        const unsigned char* b = take(count * width);
        if (b == nullptr) return {};
        for (size_t plane = 0; plane < width; ++plane) {
            for (size_t i = 0; i < count; ++i) out[i] = out[i] << 8 | b[plane * count + i];
        }
        return out;
    }

private:
    const unsigned char* data_;
    size_t size_;
    size_t pos_ = 0;
    bool ok_ = true;
};

int64_t unzigzag(uint64_t v) { return static_cast<int64_t>(v >> 1) ^ -static_cast<int64_t>(v & 1); }

std::vector<int32_t> ints(Reader& r, size_t n) {
    std::vector<int32_t> out;
    for (uint64_t v : r.interleaved(n, 4)) out.push_back(static_cast<int32_t>(unzigzag(v)));
    return out;
}

// Floats have their sign bit moved to the end.
std::vector<float> floats(Reader& r, size_t n) {
    std::vector<float> out;
    for (uint64_t v : r.interleaved(n, 4)) {
        const uint32_t bits = static_cast<uint32_t>(v >> 1) | static_cast<uint32_t>(v << 31);
        float f;
        std::memcpy(&f, &bits, 4);
        out.push_back(f);
    }
    return out;
}

std::vector<int32_t> referents(Reader& r, size_t n) {
    std::vector<int32_t> out = ints(r, n);
    for (size_t i = 1; i < out.size(); ++i) out[i] += out[i - 1];
    return out;
}

std::string number(double v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.9g", v);
    return text;
}

// CFrame rotation ids 2..36: two columns picked from +X,+Y,+Z,-X,-Y,-Z.
bool specialRotation(uint8_t id, glm::mat3& out) {
    if (id < 2) return false;
    const int index = id - 1;
    const int a = index / 6;
    const int b = index % 6;
    if (a > 5 || a % 3 == b % 3) return false;
    auto axis = [](int i) {
        glm::vec3 v(0.0f);
        v[i % 3] = i < 3 ? 1.0f : -1.0f;
        return v;
    };
    out[0] = axis(a);
    out[1] = axis(b);
    out[2] = glm::cross(out[0], out[1]);
    return true;
}

struct Instance {
    std::string className;
    std::unordered_map<std::string, std::string> properties;
    int32_t parent = -1;
    std::vector<int32_t> children;
    bool hasParentRecord = false;
};

struct Document {
    std::map<uint32_t, std::vector<int32_t>> classRefs;
    std::map<int32_t, Instance> instances;
    std::vector<int32_t> parentOrder;
};

void setProperty(Instance& inst, const std::string& name, const std::string& type, const std::string& value) {
    inst.properties[name] = value;
    inst.properties["@type." + name] = type;
}

void readProperty(Reader& r, Document& doc) {
    const uint32_t classId = r.u32();
    const std::string name = r.str();
    const uint8_t type = r.u8();
    const auto found = doc.classRefs.find(classId);
    if (!r.ok() || found == doc.classRefs.end()) return;
    const std::vector<int32_t>& refs = found->second;
    const size_t n = refs.size();
    auto each = [&](auto&& fn) {
        for (size_t i = 0; i < n && r.ok(); ++i) fn(doc.instances[refs[i]], i);
    };
    auto fields = [&](const char* typeName, std::initializer_list<const char*> names,
                      const std::vector<std::vector<float>>& columns) {
        for (size_t i = 0; i < n && r.ok(); ++i) {
            Instance& inst = doc.instances[refs[i]];
            size_t c = 0;
            for (const char* field : names) inst.properties[name + "." + field] = number(columns[c++][i]);
            inst.properties["@type." + name] = typeName;
            inst.properties[name] = "";
        }
    };

    switch (type) {
        case 0x01: // String
        case 0x1D: // ProtectedString
            each([&](Instance& inst, size_t) { setProperty(inst, name, type == 0x01 ? "string" : "ProtectedString", r.str()); });
            break;
        case 0x02: each([&](Instance& inst, size_t) { setProperty(inst, name, "bool", r.u8() != 0 ? "true" : "false"); }); break;
        case 0x03: {
            const auto values = ints(r, n);
            each([&](Instance& inst, size_t i) { setProperty(inst, name, "int", std::to_string(values[i])); });
            break;
        }
        case 0x04: {
            const auto values = floats(r, n);
            each([&](Instance& inst, size_t i) { setProperty(inst, name, "float", number(values[i])); });
            break;
        }
        case 0x05: each([&](Instance& inst, size_t) { setProperty(inst, name, "double", number(r.f64())); }); break;
        case 0x06: {
            const auto scale = floats(r, n);
            const auto offset = ints(r, n);
            std::vector<std::vector<float>> columns{scale, std::vector<float>(offset.begin(), offset.end())};
            if (r.ok()) fields("UDim", {"S", "O"}, columns);
            break;
        }
        case 0x07: {
            const auto xs = floats(r, n);
            const auto ys = floats(r, n);
            const auto xo = ints(r, n);
            const auto yo = ints(r, n);
            std::vector<std::vector<float>> columns{xs, std::vector<float>(xo.begin(), xo.end()), ys,
                                                   std::vector<float>(yo.begin(), yo.end())};
            if (r.ok()) fields("UDim2", {"XS", "XO", "YS", "YO"}, columns);
            break;
        }
        case 0x0B: // BrickColor
        case 0x12: { // Enum
            const auto values = r.interleaved(n, 4);
            each([&](Instance& inst, size_t i) {
                setProperty(inst, name, type == 0x12 ? "token" : "int", std::to_string(values[i]));
            });
            break;
        }
        case 0x0C: {
            const auto red = floats(r, n);
            const auto green = floats(r, n);
            const auto blue = floats(r, n);
            if (r.ok()) fields("Color3", {"R", "G", "B"}, {red, green, blue});
            break;
        }
        case 0x0D: {
            const auto x = floats(r, n);
            const auto y = floats(r, n);
            if (r.ok()) fields("Vector2", {"X", "Y"}, {x, y});
            break;
        }
        case 0x0E: {
            const auto x = floats(r, n);
            const auto y = floats(r, n);
            const auto z = floats(r, n);
            if (r.ok()) fields("Vector3", {"X", "Y", "Z"}, {x, y, z});
            break;
        }
        case 0x10: { // CFrame
            std::vector<glm::mat3> rotations(n, glm::mat3(1.0f));
            for (size_t i = 0; i < n && r.ok(); ++i) {
                const uint8_t id = r.u8();
                if (id == 0) {
                    // Row-major R00..R22.
                    for (int row = 0; row < 3; ++row) {
                        for (int column = 0; column < 3; ++column) rotations[i][column][row] = r.f32();
                    }
                } else if (!specialRotation(id, rotations[i])) {
                    return;
                }
            }
            const auto x = floats(r, n);
            const auto y = floats(r, n);
            const auto z = floats(r, n);
            if (!r.ok()) return;
            std::vector<std::vector<float>> columns{x, y, z};
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    std::vector<float> values(n);
                    for (size_t i = 0; i < n; ++i) values[i] = rotations[i][column][row];
                    columns.push_back(std::move(values));
                }
            }
            fields("CoordinateFrame", {"X", "Y", "Z", "R00", "R01", "R02", "R10", "R11", "R12", "R20", "R21", "R22"},
                   columns);
            break;
        }
        case 0x13: { // Referent
            const auto values = referents(r, n);
            each([&](Instance& inst, size_t i) {
                setProperty(inst, name, "Ref", values[i] < 0 ? "null" : "RBX" + std::to_string(values[i]));
            });
            break;
        }
        case 0x1A: { // Color3uint8: all reds, then greens, then blues
            const unsigned char* bytes = r.take(n * 3);
            if (bytes == nullptr) return;
            each([&](Instance& inst, size_t i) {
                const uint32_t packed = 0xFF000000u | static_cast<uint32_t>(bytes[i]) << 16 |
                                        static_cast<uint32_t>(bytes[n + i]) << 8 | bytes[2 * n + i];
                setProperty(inst, name, "Color3uint8", std::to_string(packed));
            });
            break;
        }
        case 0x1B: { // Int64
            const auto values = r.interleaved(n, 8);
            each([&](Instance& inst, size_t i) { setProperty(inst, name, "int64", std::to_string(unzigzag(values[i]))); });
            break;
        }
        default: break; // a type the importer doesn't read; the chunk holds only this property
    }
}

void readInstances(Reader& r, Document& doc) {
    const uint32_t classId = r.u32();
    const std::string className = r.str();
    const uint8_t format = r.u8();
    const uint32_t count = r.u32();
    if (!r.ok() || count > r.left()) return;
    std::vector<int32_t> refs = referents(r, count);
    if (format == 1) (void)r.take(count);
    for (int32_t ref : refs) doc.instances[ref].className = className;
    doc.classRefs[classId] = std::move(refs);
}

void readParents(Reader& r, Document& doc) {
    (void)r.u8();
    const uint32_t count = r.u32();
    if (!r.ok() || count > r.left()) return;
    const auto children = referents(r, count);
    const auto parents = referents(r, count);
    for (size_t i = 0; i < children.size() && i < parents.size(); ++i) {
        const auto child = doc.instances.find(children[i]);
        if (child == doc.instances.end()) continue;
        child->second.parent = parents[i];
        child->second.hasParentRecord = true;
        doc.parentOrder.push_back(children[i]);
        if (const auto parent = doc.instances.find(parents[i]); parent != doc.instances.end()) {
            parent->second.children.push_back(children[i]);
        }
    }
}

ImportedInstance build(Document& doc, int32_t ref, int depth) {
    Instance& inst = doc.instances[ref];
    ImportedInstance out;
    out.className = inst.className;
    out.referent = "RBX" + std::to_string(ref);
    out.properties = std::move(inst.properties);
    const auto name = out.properties.find("Name");
    out.name = name != out.properties.end() && !name->second.empty() ? name->second : out.className;
    if (depth < 1000) {
        for (int32_t child : inst.children) out.children.push_back(build(doc, child, depth + 1));
    }
    return out;
}

} // namespace

bool isRobloxBinary(const std::string& data) { return data.compare(0, 8, kMagic) == 0; }

bool lz4Decompress(const unsigned char* src, size_t srcSize, unsigned char* dst, size_t dstSize) {
    size_t ip = 0;
    size_t op = 0;
    auto length = [&](size_t base) -> size_t {
        size_t total = base;
        if (base != 15) return total;
        while (ip < srcSize) {
            const unsigned char b = src[ip++];
            total += b;
            if (b != 255) break;
        }
        return total;
    };
    while (ip < srcSize) {
        const unsigned char token = src[ip++];
        const size_t literals = length(token >> 4);
        if (literals > srcSize - ip || literals > dstSize - op) return false;
        std::memcpy(dst + op, src + ip, literals);
        ip += literals;
        op += literals;
        if (ip >= srcSize) break; // the last sequence has no match
        if (srcSize - ip < 2) return false;
        const size_t offset = src[ip] | src[ip + 1] << 8;
        ip += 2;
        if (offset == 0 || offset > op) return false;
        const size_t match = length(token & 15) + 4;
        if (match > dstSize - op) return false;
        for (size_t i = 0; i < match; ++i, ++op) dst[op] = dst[op - offset];
    }
    return op == dstSize;
}

bool readRobloxBinary(const std::string& data, std::vector<ImportedInstance>& out, std::string& error) {
    Reader file(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    const unsigned char* magic = file.take(8);
    const unsigned char* signature = file.take(6);
    if (magic == nullptr || std::memcmp(magic, kMagic, 8) != 0 || signature == nullptr ||
        std::memcmp(signature, kSignature, 6) != 0) {
        error = "not a Roblox binary file (bad header)";
        return false;
    }
    if (file.u16() != 0) {
        error = "unsupported Roblox binary version";
        return false;
    }
    (void)file.u32(); // class count
    (void)file.u32(); // instance count
    (void)file.take(8);

    Document doc;
    bool ended = false;
    std::vector<unsigned char> buffer;
    while (file.ok() && !file.atEnd() && !ended) {
        const unsigned char* nameBytes = file.take(4);
        const uint32_t compressed = file.u32();
        const uint32_t size = file.u32();
        (void)file.take(4);
        if (!file.ok() || size > kMaxChunkBytes) break;
        const std::string chunk(reinterpret_cast<const char*>(nameBytes), strnlen(reinterpret_cast<const char*>(nameBytes), 4));
        const unsigned char* body = nullptr;
        if (compressed == 0) {
            body = file.take(size);
        } else {
            const unsigned char* packed = file.take(compressed);
            if (packed == nullptr) break;
            if (compressed >= 4 && packed[0] == 0x28 && packed[1] == 0xB5 && packed[2] == 0x2F && packed[3] == 0xFD) {
                error = "this file uses Zstandard compression, which Kronos can't read yet; save it again from Roblox "
                        "Studio, or save it as .rbxlx";
                return false;
            }
            buffer.assign(size, 0);
            if (!lz4Decompress(packed, compressed, buffer.data(), size)) {
                error = "a " + chunk + " chunk is damaged (LZ4)";
                return false;
            }
            body = buffer.data();
        }
        if (body == nullptr) break;
        Reader r(body, size);
        if (chunk == "INST") readInstances(r, doc);
        else if (chunk == "PROP") readProperty(r, doc);
        else if (chunk == "PRNT") readParents(r, doc);
        else if (chunk == "END") ended = true;
        if (!r.ok()) {
            error = "a " + chunk + " chunk is damaged";
            return false;
        }
    }
    if (!ended) {
        error = "the file is cut short (no END chunk)";
        return false;
    }

    out.clear();
    for (int32_t ref : doc.parentOrder) {
        const Instance& inst = doc.instances[ref];
        if (inst.parent < 0 || doc.instances.count(inst.parent) == 0) out.push_back(build(doc, ref, 0));
    }
    // Instances with no PRNT record count as top level too.
    for (auto& [ref, inst] : doc.instances) {
        if (!inst.hasParentRecord && !inst.className.empty()) out.push_back(build(doc, ref, 0));
    }
    return true;
}

} // namespace engine::migration
