#include "network_config.h"
#include "json_helpers.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
namespace wm {
namespace {
void secure_wipe(void *p, size_t n) {
    auto *b = static_cast<volatile unsigned char *>(p);
    while (n--)
        *b++ = 0;
}
void clear(cJSON *p) {
    for (auto *a = p; a; a = a->next) {
        if (a->child)
            clear(a->child);
        if (a->valuestring)
            secure_wipe(a->valuestring, std::strlen(a->valuestring));
    }
}
cJSON *read(const char *root, const char *file) {
    char path[512];
    int n = std::snprintf(path, sizeof(path), "%s/%s", root, file);
    if (n < 0 || size_t(n) >= sizeof(path))
        return nullptr;
    auto *f = std::fopen(path, "rb");
    if (!f)
        return nullptr;
    auto *buf = static_cast<char *>(std::malloc(8193));
    if (!buf) {
        std::fclose(f);
        return nullptr;
    }
    size_t bytes = std::fread(buf, 1, 8193, f);
    bool ok = bytes && bytes <= 8192 && !std::ferror(f);
    std::fclose(f);
    auto *p = ok ? document(buf, bytes, 8192) : nullptr;
    secure_wipe(buf, 8193);
    std::free(buf);
    return p;
}
void networks(const cJSON *root, cm::AppConfig &c) {
    auto *list = item(root, "wifi");
    if (!cJSON_IsArray(list))
        return;
    for (auto *p = list->child; p && c.wifi_count < cm::max_wifi; p = p->next) {
        auto *ssid = item(p, "ssid"), *pass = item(p, "password");
        if (!cJSON_IsString(ssid) || !cJSON_IsString(pass))
            continue;
        size_t a = std::strlen(ssid->valuestring), b = std::strlen(pass->valuestring);
        if (!a || a > 32 || b < 8 || b > 64)
            continue;
        bool bad = false;
        for (const auto *s : {ssid->valuestring, pass->valuestring})
            for (auto *q = reinterpret_cast<const unsigned char *>(s); *q; ++q)
                bad |= *q < 32 || *q == 127;
        if (b == 64)
            for (const char *q = pass->valuestring; *q; ++q)
                bad |= !((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'f') ||
                         (*q >= 'A' && *q <= 'F'));
        bool duplicate = false;
        for (unsigned i = 0; i < c.wifi_count; ++i)
            duplicate |= !std::strcmp(c.wifi[i].ssid, ssid->valuestring);
        if (bad || duplicate)
            continue;
        auto &w = c.wifi[c.wifi_count++];
        std::strcpy(w.ssid, ssid->valuestring);
        std::strcpy(w.password, pass->valuestring);
        auto *priority = item(p, "priority");
        if (integer(priority, -1000, 1000))
            w.priority = priority->valueint;
    }
}
} // namespace
void load_networks(cm::AppConfig &out, const char *root) {
    secure_wipe(&out, sizeof(out));
    auto *a = read(root, "file-server/net-a.json"), *b = read(root, "file-server/net-b.json");
    auto gen = [](const cJSON *p) {
        auto *g = item(p, "generation");
        return integer(g, 0, 9007199254740991.) ? g->valuedouble : -1.;
    };
    if (gen(b) >= gen(a)) {
        networks(b, out);
        if (!out.wifi_count)
            networks(a, out);
    } else {
        networks(a, out);
        if (!out.wifi_count)
            networks(b, out);
    }
    for (auto *p : {a, b}) {
        clear(p);
        cJSON_Delete(p);
    }
    for (const char *file : {"terminal-micro/config.json", "contextual-micro/config.json"}) {
        auto *p = read(root, file);
        networks(p, out);
        clear(p);
        cJSON_Delete(p);
    }
}
} // namespace wm

// Shared Wi-Fi manager's credential-erasure contract, without linking the SSH parser.
namespace cm {
void wipe(void *p, size_t n) {
    auto *bytes = static_cast<volatile unsigned char *>(p);
    while (n--)
        *bytes++ = 0;
}
} // namespace cm
