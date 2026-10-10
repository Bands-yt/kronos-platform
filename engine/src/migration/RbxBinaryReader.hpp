#pragma once

#include <string>
#include <vector>

#include "migration/InstanceTreeBuilder.hpp"

namespace engine::migration {

// Roblox Studio's binary place/model format (.rbxl/.rbxm).
[[nodiscard]] bool isRobloxBinary(const std::string& data);

// Builds the same tree (and property strings) the .rbxlx reader makes, so
// the rest of the import is shared. False with `error` set on bad input.
bool readRobloxBinary(const std::string& data, std::vector<ImportedInstance>& out, std::string& error);

// LZ4 block decompression, as used by the binary format's chunks.
bool lz4Decompress(const unsigned char* src, size_t srcSize, unsigned char* dst, size_t dstSize);

} // namespace engine::migration
