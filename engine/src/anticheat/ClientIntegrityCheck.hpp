#pragma once

#include <string>
#include <vector>

#include "anticheat/ExploitSignatureDB.hpp"

namespace engine::anticheat {

// One matched signature, kept simple (just the label + which observed
// value triggered it) since this is for audit/logging, not automated
// action -- see class comment below on why.
struct FlaggedSignature {
    std::string label;
    std::string observedValue;
};

struct ClientIntegrityReport {
    bool passed = true;
    std::vector<FlaggedSignature> flagged;
};

// Sprint 12 task 3's "Add basic client integrity checks" -- the real
// integration point combining the two already-real anticheat:: pieces
// (DeviceFingerprint, ExploitSignatureDB) into one usable API, per
// docs/ARCHITECTURE.md §11's pillars 2-3. What this deliberately does
// NOT do: actually enumerate a client's running processes/loaded
// modules/window titles -- that's real, invasive, OS-specific code
// ExploitSignatureDB.hpp's own header comment already says belongs
// behind a platform_adapters-style per-OS backend once one exists, not
// hardcoded here. `check()` takes whatever a caller already collected
// (today: nothing real collects this -- a client-side collector is
// still a stated future piece) and matches it against the real
// signature DB; the server-side matching logic this class provides is
// real even though the client-side collection feeding it isn't yet.
class ClientIntegrityCheck {
public:
    ClientIntegrityCheck() = default;

    // Real signature matching over whatever the caller observed. Each of
    // the three lists corresponds to one SignatureKind (ProcessName/
    // ModuleName/WindowTitle) -- see ExploitSignatureDB.hpp.
    [[nodiscard]] ClientIntegrityReport check(const std::vector<std::string>& observedProcessNames,
                                               const std::vector<std::string>& observedModuleNames,
                                               const std::vector<std::string>& observedWindowTitles) const;

    [[nodiscard]] ExploitSignatureDB& signatureDb() { return signatureDb_; }
    [[nodiscard]] const ExploitSignatureDB& signatureDb() const { return signatureDb_; }

private:
    ExploitSignatureDB signatureDb_;
};

} // namespace engine::anticheat
