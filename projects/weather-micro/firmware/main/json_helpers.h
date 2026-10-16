#pragma once
#include "cJSON.h"
#include "json_guard.h"
#include <cmath>
#include <cstring>
namespace wm {
inline const cJSON *item(const cJSON *p, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(p, key);
}
inline bool number(const cJSON *p, double low, double high) {
    return cJSON_IsNumber(p) && std::isfinite(p->valuedouble) && p->valuedouble >= low &&
           p->valuedouble <= high;
}
inline bool integer(const cJSON *p, double low, double high) {
    return number(p, low, high) && std::floor(p->valuedouble) == p->valuedouble;
}
inline bool unique_json(const cJSON *p, unsigned depth = 0) {
    if (depth > 8)
        return false;
    for (auto *a = p->child; a; a = a->next) {
        if (cJSON_IsObject(p))
            for (auto *b = a->next; b; b = b->next)
                if (a->string && b->string && !std::strcmp(a->string, b->string))
                    return false;
        if (!unique_json(a, depth + 1))
            return false;
    }
    return true;
}
inline cJSON *document(const char *text, size_t n, size_t limit = 24576) {
    if (n > limit || !json_guard(text, n, 8))
        return nullptr;
    const char *end = nullptr;
    auto *p = cJSON_ParseWithLengthOpts(text, n, &end, 0);
    if (!p)
        return nullptr;
    const char *last = text + n;
    while (end < last && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        ++end;
    if (end != last || !cJSON_IsObject(p) || !unique_json(p)) {
        cJSON_Delete(p);
        return nullptr;
    }
    return p;
}
struct Json {
    cJSON *p;
    ~Json() { cJSON_Delete(p); }
    explicit operator bool() const { return p != nullptr; }
};
inline bool unit(const cJSON *units, const char *key, const char *expected) {
    auto *p = item(units, key);
    return cJSON_IsString(p) && !std::strcmp(p->valuestring, expected);
}
} // namespace wm
