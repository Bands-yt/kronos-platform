#include "net/NetworkIdentity.hpp"

namespace engine::net {

namespace {
uint32_t g_nextNetworkId = 1; // 0 is reserved (NetworkIdentity's default, "not yet assigned")
}

uint32_t allocateNetworkId() { return g_nextNetworkId++; }

} // namespace engine::net
