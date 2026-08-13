#include "moderation/TrustedCreatorRegistry.hpp"

namespace engine::moderation {

void TrustedCreatorRegistry::setTrusted(net::PlayerId creator, bool trusted) {
    if (trusted) {
        trusted_.insert(creator);
    } else {
        trusted_.erase(creator);
    }
}

bool TrustedCreatorRegistry::isTrusted(net::PlayerId creator) const { return trusted_.count(creator) > 0; }

} // namespace engine::moderation
