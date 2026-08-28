#include "polyglot/UnifiedTypeSystem.hpp"

#include <algorithm>

namespace engine::polyglot {

uint32_t fieldTypeByteSize(FieldType type) {
    switch (type) {
        case FieldType::F32: return 4;
        case FieldType::F64: return 8;
        case FieldType::I32: return 4;
        case FieldType::I64: return 8;
        case FieldType::Bool: return 1;
        case FieldType::Vec2: return 8;
        case FieldType::Vec3: return 12;
        case FieldType::Vec4: return 16;
        case FieldType::Quat: return 16;
        case FieldType::FixedString: return 0; // real, honest "caller-defined size" case, see below
    }
    return 0;
}

bool TypeRegistry::registerComponent(ComponentDescriptor descriptor) {
    if (descriptor.componentName.empty()) return false;
    if (descriptor.totalByteSize == 0) return false;
    if (find(descriptor.componentName) != nullptr) return false; // no silent overwrite of an existing name

    for (const FieldDescriptor& field : descriptor.fields) {
        if (field.name.empty()) return false;
        // FixedString has no single real byte size (fieldTypeByteSize()
        // real-returns 0 for it, see above) -- a field of that type
        // supplies its own real byteSize instead, which just needs to be
        // nonzero and to actually fit.
        if (field.type != FieldType::FixedString) {
            if (field.byteSize != fieldTypeByteSize(field.type)) return false;
        } else if (field.byteSize == 0) {
            return false;
        }
        // Real overflow-safe range check -- byteOffset + byteSize could
        // wrap a 32-bit uint32_t for a maliciously/accidentally huge
        // input; comparing via subtraction from totalByteSize instead of
        // addition avoids ever computing that overflowing sum.
        if (field.byteOffset > descriptor.totalByteSize) return false;
        if (field.byteSize > descriptor.totalByteSize - field.byteOffset) return false;
    }

    components_.push_back(std::move(descriptor));
    return true;
}

const ComponentDescriptor* TypeRegistry::find(const std::string& componentName) const {
    auto it = std::find_if(components_.begin(), components_.end(),
                            [&](const ComponentDescriptor& c) { return c.componentName == componentName; });
    return it != components_.end() ? &(*it) : nullptr;
}

} // namespace engine::polyglot
