#include "private_config.h"
#include "cJSON.h"
#include "json_guard.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
namespace ou {
void wipe(void *p, size_t n) {
    volatile unsigned char *b = static_cast<volatile unsigned char *>(p);
    while (n--)
        *b++ = 0;
}
namespace {
void wipe_tree(cJSON *p) {
    for (auto *q = p; q; q = q->next) {
        if (q->child)
            wipe_tree(q->child);
        if (q->valuestring)
            wipe(q->valuestring, std::strlen(q->valuestring));
    }
}
bool unique(const cJSON *p, unsigned depth = 0) {
    if (depth > 8)
        return false;
    for (auto *a = p->child; a; a = a->next) {
        if (cJSON_IsObject(p))
            for (auto *b = a->next; b; b = b->next)
                if (a->string && b->string && !std::strcmp(a->string, b->string))
                    return false;
        if (!unique(a, depth + 1))
            return false;
    }
    return true;
}
unsigned networks(const cJSON *list, cm::AppConfig &c) {
    if (!cJSON_IsArray(list))
        return 0;
    for (auto *p = list->child; p && c.wifi_count < cm::max_wifi; p = p->next) {
        auto *ssid = cJSON_GetObjectItemCaseSensitive(p, "ssid");
        auto *pass = cJSON_GetObjectItemCaseSensitive(p, "password");
        auto *priority = cJSON_GetObjectItemCaseSensitive(p, "priority");
        if (!cJSON_IsString(ssid) || !cJSON_IsString(pass))
            continue;
        size_t a = std::strlen(ssid->valuestring), b = std::strlen(pass->valuestring);
        if (!a || a > 32 || b < 8 || b > 64)
            continue;
        bool controls = false;
        for (const unsigned char *s = reinterpret_cast<unsigned char *>(ssid->valuestring); *s; ++s)
            controls |= *s < 32 || *s == 127;
        for (const unsigned char *s = reinterpret_cast<unsigned char *>(pass->valuestring); *s; ++s)
            controls |= *s < 32 || *s == 127;
        if (b == 64)
            for (const char *s = pass->valuestring; *s; ++s)
                controls |= !((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') ||
                              (*s >= 'A' && *s <= 'F'));
        if (controls)
            continue;
        bool dup = false;
        for (size_t i = 0; i < c.wifi_count; ++i)
            dup |= !std::strcmp(c.wifi[i].ssid, ssid->valuestring);
        if (dup)
            continue;
        auto &n = c.wifi[c.wifi_count++];
        std::strcpy(n.ssid, ssid->valuestring);
        std::strcpy(n.password, pass->valuestring);
        if (cJSON_IsNumber(priority) && std::isfinite(priority->valuedouble) &&
            std::floor(priority->valuedouble) == priority->valuedouble &&
            priority->valuedouble >= -1000 && priority->valuedouble <= 1000)
            n.priority = priority->valueint;
    }
    return c.wifi_count;
}
cJSON *read(const char *root_path, const char *relative) {
    char path[512];
    int size = std::snprintf(path, sizeof(path), "%s/%s", root_path, relative);
    if (size < 0 || size >= int(sizeof(path)))
        return nullptr;
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return nullptr;
    auto *text = static_cast<char *>(std::malloc(8193));
    if (!text) {
        std::fclose(f);
        return nullptr;
    }
    size_t n = std::fread(text, 1, 8193, f);
    bool ok = !std::ferror(f) && n && n <= 8192 && json_guard(text, n, 8);
    std::fclose(f);
    cJSON *root = nullptr;
    if (ok) {
        text[n] = 0;
        const char *end = nullptr;
        root = cJSON_ParseWithLengthOpts(text, n + 1, &end, 1);
        if (root && (!cJSON_IsObject(root) || !unique(root))) {
            wipe_tree(root);
            cJSON_Delete(root);
            root = nullptr;
        }
    }
    wipe(text, 8193);
    std::free(text);
    return root;
}
void fallback(Config &c, const char *root_path) {
    auto *a = read(root_path, "file-server/net-a.json");
    auto *b = read(root_path, "file-server/net-b.json");
    auto generation = [](cJSON *p) {
        auto *n = cJSON_GetObjectItemCaseSensitive(p, "generation");
        return cJSON_IsNumber(n) ? n->valuedouble : -1;
    };
    cJSON *first = generation(b) >= generation(a) ? b : a;
    cJSON *second = first == b ? a : b;
    if (first)
        networks(cJSON_GetObjectItemCaseSensitive(first, "wifi"), c.network);
    if (!c.network.wifi_count && second)
        networks(cJSON_GetObjectItemCaseSensitive(second, "wifi"), c.network);
    for (auto *p : {a, b}) {
        if (p) {
            wipe_tree(p);
            cJSON_Delete(p);
        }
    }
    for (const char *path : {"terminal-micro/config.json", "contextual-micro/config.json"}) {
        auto *root = read(root_path, path);
        if (root) {
            networks(cJSON_GetObjectItemCaseSensitive(root, "wifi"), c.network);
            wipe_tree(root);
            cJSON_Delete(root);
        }
    }
}
} // namespace
bool parse_private_config(const char *s, size_t n, Config &out) {
    wipe(&out, sizeof(out));
    std::strcpy(out.network.device_name, "cyd-ollama");
    if (!s || !n || n > 8192 || std::memchr(s, 0, n))
        return false;
    if (!json_guard(s, n, 8))
        return false;
    const char *end = nullptr;
    auto *root = cJSON_ParseWithLengthOpts(s, n, &end, 0);
    bool valid = root && cJSON_IsObject(root) && unique(root);
    if (valid) {
        while (end < s + n && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t'))
            ++end;
        valid = end == s + n;
    }
    if (valid)
        for (auto *p = root->child; p; p = p->next)
            if (std::strcmp(p->string, "api_key") && std::strcmp(p->string, "wifi"))
                valid = false;
    if (valid) {
        out.file_loaded = true;
        auto *k = cJSON_GetObjectItemCaseSensitive(root, "api_key");
        if (k && !cJSON_IsString(k))
            valid = false;
        if (cJSON_IsString(k)) {
            const size_t size = std::strlen(k->valuestring);
            bool safe = size >= 16 && size <= 512;
            for (const unsigned char *p = reinterpret_cast<unsigned char *>(k->valuestring); *p;
                 ++p)
                if (*p < 33 || *p > 126)
                    safe = false;
            if (std::strstr(k->valuestring, "YOUR_") || std::strstr(k->valuestring, "REPLACE"))
                safe = false;
            if (safe) {
                std::strcpy(out.api_key, k->valuestring);
                out.key_ready = true;
            }
        }
        auto *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
        if (wifi && !cJSON_IsArray(wifi))
            valid = false;
        else if (wifi)
            networks(wifi, out.network);
    }
    if (root) {
        wipe_tree(root);
        cJSON_Delete(root);
    }
    if (!valid) {
        wipe(&out, sizeof(out));
        std::strcpy(out.network.device_name, "cyd-ollama");
    }
    return valid;
}
void load_private_config(Config &c, const char *root_path) {
    auto *text = static_cast<char *>(std::malloc(8193));
    wipe(&c, sizeof(c));
    std::strcpy(c.network.device_name, "cyd-ollama");
    if (text) {
        char path[512];
        int size = std::snprintf(path, sizeof(path), "%s/private/ollama.json", root_path);
        FILE *f = size >= 0 && size < int(sizeof(path)) ? std::fopen(path, "rb") : nullptr;
        if (f) {
            size_t n = std::fread(text, 1, 8193, f);
            if (!std::ferror(f) && n <= 8192)
                parse_private_config(text, n, c);
            std::fclose(f);
        }
        wipe(text, 8193);
        std::free(text);
    }
    if (!c.network.wifi_count)
        fallback(c, root_path);
}
} // namespace ou
// Reused shared Wi-Fi implementation's zeroisation hook, never credential logging.
namespace cm {
void wipe(void *p, size_t n) { ou::wipe(p, n); }
} // namespace cm
