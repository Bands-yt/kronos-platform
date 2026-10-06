#pragma once

#include <string>
#include <vector>

#include "core/HotReloadState.hpp"

// Fixture state for StatefulModule*.cpp. V1 is shared by modules A and B
// (same layout, different code); V2 adds a field, so loading module C over
// either forces a migration.
struct StatefulCounter {
    int ticks = 0;
    std::vector<int> history;
    std::string label = "fresh";
};

struct StatefulCounterV2 {
    long long ticks = 0;
    std::vector<int> history;
    std::string label = "fresh";
    int migratedFromVersion = 0;
};

// What the test reads back from the ECS.
struct StatefulProbe {
    int ticks = 0;
    int historySize = 0;
    int loads = 0;
    std::string label;
};
