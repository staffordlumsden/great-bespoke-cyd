#pragma once
#include <cstddef>
namespace ou {
// Preflight before recursive cJSON allocation. Escaped NUL is not representable
// safely in cJSON strings; reject it, including object keys.
inline bool json_guard(const char *s, size_t n, unsigned limit) {
    if (!s || !n)
        return false;
    unsigned depth = 0;
    bool quoted = false, escape = false;
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        if (!c)
            return false;
        if (quoted) {
            if (escape) {
                if (c == 'u' && i + 4 < n && s[i + 1] == '0' && s[i + 2] == '0' &&
                    s[i + 3] == '0' && s[i + 4] == '0')
                    return false;
                escape = false;
            } else if (c == '\\')
                escape = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > limit)
                return false;
        } else if (c == '}' || c == ']') {
            if (!depth)
                return false;
            --depth;
        }
    }
    return !quoted && !escape && !depth;
}
} // namespace ou
