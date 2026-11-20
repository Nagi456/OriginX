// A set of shared values (id, value) and the diff shown on the confirm screen
// (docs/config-sharing-design.md §5.2). Windows-independent.
#pragma once

#include "Registry.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace ConfigShare {

// Sorted by id, no duplicates.
struct Snapshot {
    std::vector<std::pair<uint16_t, int32_t>> values;

    const int32_t* Find(uint16_t id) const
    {
        auto it = std::lower_bound(values.begin(), values.end(), id,
                                   [](const std::pair<uint16_t, int32_t>& p, uint16_t v) { return p.first < v; });
        return (it != values.end() && it->first == id) ? &it->second : nullptr;
    }

    void Set(uint16_t id, int32_t value)
    {
        auto it = std::lower_bound(values.begin(), values.end(), id,
                                   [](const std::pair<uint16_t, int32_t>& p, uint16_t v) { return p.first < v; });
        if (it != values.end() && it->first == id) it->second = value;
        else values.insert(it, {id, value});
    }
};

inline int CategoryIndex(Category c)
{
    switch (c) {
    case Category::Modules: return 0;
    case Category::Hud: return 1;
    case Category::Theme: return 2;
    case Category::Lite: return 3;
    }
    return 0;
}

struct DiffResult {
    int changed[4] = {0, 0, 0, 0};       // per category, indexed by CategoryIndex
    std::vector<uint16_t> turnedOn;      // module ids going off -> on
    std::vector<uint16_t> turnedOff;     // module ids going on -> off

    int Total() const { return changed[0] + changed[1] + changed[2] + changed[3]; }
};

// Only values present in `incoming`, known to the registry and in a selected
// category are counted. A value missing from `now` counts as a change.
inline DiffResult Diff(const Snapshot& now, const Snapshot& incoming, uint8_t categories)
{
    DiffResult r;
    for (const auto& [id, value] : incoming.values) {
        const Field* f = FindById(id);
        if (!f || !(categories & static_cast<uint8_t>(f->category))) continue;
        const int32_t* cur = now.Find(id);
        if (cur && *cur == value) continue;
        ++r.changed[CategoryIndex(f->category)];
        if (f->category == Category::Modules) {
            if (value) r.turnedOn.push_back(id);
            else r.turnedOff.push_back(id);
        }
    }
    return r;
}

} // namespace ConfigShare
