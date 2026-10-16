#pragma once
#include "weather_model.h"
namespace wm {
struct Cache {
    int64_t id = 0;
    Forecast forecast{};
    bool has = false;
    unsigned failures = 0;
    uint64_t updated = 0, due = 0, manual_after = 0;
    char error[45]{};
};
class CacheBook {
    Cache entries[max_places]{};

  public:
    Cache *find(int64_t id, bool create = true) {
        if (id <= 0)
            return nullptr;
        for (auto &c : entries)
            if (c.id == id)
                return &c;
        if (create)
            for (auto &c : entries)
                if (!c.id) {
                    c = Cache{};
                    c.id = id;
                    return &c;
                }
        return nullptr;
    }
    Cache *response(const Settings &s, int64_t id) {
        for (unsigned i = 0; i < s.count; ++i)
            if (s.places[i].id == id)
                return find(id);
        return nullptr;
    }
    void prune(const Settings &s) {
        for (auto &c : entries) {
            bool exists = false;
            for (unsigned i = 0; i < s.count; ++i)
                exists |= s.places[i].id == c.id;
            if (!exists)
                c = Cache{};
        }
    }
};
} // namespace wm
