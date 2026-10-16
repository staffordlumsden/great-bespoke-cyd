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

namespace {
bool text_value(const cJSON *p, char *out, size_t cap, bool empty = false) {
    if (!cJSON_IsString(p))
        return false;
    size_t n = std::strlen(p->valuestring);
    if ((!n && !empty) || n >= cap)
        return false;
    for (const unsigned char *q = reinterpret_cast<const unsigned char *>(p->valuestring); *q; ++q)
        if (*q < 32 || *q > 126)
            return false;
    std::memcpy(out, p->valuestring, n + 1);
    return true;
}
bool token(const char *value) {
    size_t n = std::strlen(value);
    if (n < 16 || n > 512 || std::strstr(value, "YOUR_") || std::strstr(value, "REPLACE"))
        return false;
    for (const unsigned char *q = reinterpret_cast<const unsigned char *>(value); *q; ++q)
        if (*q < 33 || *q > 126)
            return false;
    return true;
}
bool https_url(const char *s) {
    if (std::strncmp(s, "https://", 8))
        return false;
    const char *host = s + 8;
    const char *slash = std::strchr(host, '/');
    if (!slash || slash == host)
        return false;
    const char *colon = nullptr;
    for (const char *p = host; p < slash; ++p) {
        if (*p == ':') {
            if (colon)
                return false;
            colon = p;
        } else if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                     (*p >= '0' && *p <= '9') || *p == '.' || *p == '-'))
            return false;
    }
    if (colon) {
        unsigned port = 0;
        if (colon == host || colon + 1 == slash)
            return false;
        for (auto *p = colon + 1; p < slash; ++p) {
            if (*p < '0' || *p > '9')
                return false;
            port = port * 10 + *p - '0';
            if (port > 65535)
                return false;
        }
        if (!port)
            return false;
    }
    for (const char *p = slash; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '/' || *p == '_' || *p == '-' || *p == '.'))
            return false;
    return !std::strstr(slash, "..");
}
bool profile(const cJSON *p, ProviderConfig &out) {
    if (!cJSON_IsObject(p))
        return false;
    for (auto *k = p->child; k; k = k->next)
        if (std::strcmp(k->string, "type") && std::strcmp(k->string, "id") &&
            std::strcmp(k->string, "label") && std::strcmp(k->string, "api_key") &&
            std::strcmp(k->string, "url") && std::strcmp(k->string, "ca_file"))
            return false;
    char type[17];
    if (!text_value(cJSON_GetObjectItemCaseSensitive(p, "type"), type, sizeof(type)))
        return false;
    if (!std::strcmp(type, "ollama"))
        out.kind = ProviderKind::ollama;
    else if (!std::strcmp(type, "codex"))
        out.kind = ProviderKind::codex;
    else if (!std::strcmp(type, "openrouter"))
        out.kind = ProviderKind::openrouter;
    else if (!std::strcmp(type, "feed"))
        out.kind = ProviderKind::feed;
    else
        return false;
    auto *id = cJSON_GetObjectItemCaseSensitive(p, "id");
    if (id) {
        if (!text_value(id, out.id, sizeof(out.id)))
            return false;
    } else
        std::strcpy(out.id, type);
    for (const char *q = out.id; *q; ++q)
        if (!((*q >= 'a' && *q <= 'z') || (*q >= '0' && *q <= '9') || *q == '_' || *q == '-'))
            return false;
    auto *label = cJSON_GetObjectItemCaseSensitive(p, "label");
    if (label) {
        if (!text_value(label, out.label, sizeof(out.label)))
            return false;
    } else
        std::strcpy(out.label, out.kind == ProviderKind::ollama       ? "Ollama"
                               : out.kind == ProviderKind::codex      ? "Codex"
                               : out.kind == ProviderKind::openrouter ? "OpenRouter"
                                                                      : "Custom feed");
    auto *key = cJSON_GetObjectItemCaseSensitive(p, "api_key");
    if (key && !text_value(key, out.api_key, sizeof(out.api_key), true))
        return false;
    out.ready = token(out.api_key);
    if (out.kind == ProviderKind::codex || out.kind == ProviderKind::feed) {
        if (!text_value(cJSON_GetObjectItemCaseSensitive(p, "url"), out.url, sizeof(out.url)) ||
            !https_url(out.url))
            return false;
        auto *ca = cJSON_GetObjectItemCaseSensitive(p, "ca_file");
        if (ca) {
            if (!text_value(ca, out.ca_file, sizeof(out.ca_file)) ||
                std::strncmp(out.ca_file, "/private/", 9) || std::strstr(out.ca_file, "..") ||
                std::strchr(out.ca_file, '\\'))
                return false;
        }
    }
    return true;
}
} // namespace
bool parse_private_config(const char *s, size_t n, Config &out) {
    wipe(&out, sizeof(out));
    std::strcpy(out.network.device_name, "cyd-usage");
    if (!s || !n || n > 8192 || !json_guard(s, n, 8))
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
        for (auto *k = root->child; k; k = k->next)
            if (std::strcmp(k->string, "api_key") && std::strcmp(k->string, "wifi") &&
                std::strcmp(k->string, "providers"))
                valid = false;
    if (valid) {
        auto *list = cJSON_GetObjectItemCaseSensitive(root, "providers");
        auto *legacy = cJSON_GetObjectItemCaseSensitive(root, "api_key");
        if (list) {
            if (legacy || !cJSON_IsArray(list) || cJSON_GetArraySize(list) > int(max_providers))
                valid = false;
            else
                for (auto *item = list->child; item && valid; item = item->next) {
                    auto &dest = out.providers[out.provider_count];
                    if (!profile(item, dest)) {
                        valid = false;
                        break;
                    }
                    for (unsigned i = 0; i < out.provider_count; ++i)
                        if (!std::strcmp(out.providers[i].id, dest.id))
                            valid = false;
                    if (valid)
                        ++out.provider_count;
                }
        } else {
            if (legacy && !text_value(legacy, out.api_key, sizeof(out.api_key), true))
                valid = false;
            if (valid) {
                auto &dest = out.providers[out.provider_count++];
                std::strcpy(dest.id, "ollama");
                std::strcpy(dest.label, "Ollama");
                std::strcpy(dest.api_key, out.api_key);
                dest.ready = token(dest.api_key);
                out.key_ready = dest.ready;
            }
        }
        auto *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
        if (wifi && !cJSON_IsArray(wifi))
            valid = false;
        else if (wifi)
            networks(wifi, out.network);
        out.file_loaded = valid;
    }
    if (root) {
        wipe_tree(root);
        cJSON_Delete(root);
    }
    if (!valid) {
        wipe(&out, sizeof(out));
        std::strcpy(out.network.device_name, "cyd-usage");
    }
    return valid;
}
void load_private_config(Config &c, const char *root_path) {
    auto *text = static_cast<char *>(std::malloc(8193));
    wipe(&c, sizeof(c));
    std::strcpy(c.network.device_name, "cyd-usage");
    if (text) {
        char path[512];
        int size = std::snprintf(path, sizeof(path), "%s/private/usage.json", root_path);
        FILE *f = size >= 0 && size < int(sizeof(path)) ? std::fopen(path, "rb") : nullptr;
        if (!f) {
            std::snprintf(path, sizeof(path), "%s/private/ollama.json", root_path);
            f = std::fopen(path, "rb");
        }
        if (f) {
            size_t n = std::fread(text, 1, 8193, f);
            if (!std::ferror(f) && n <= 8192)
                parse_private_config(text, n, c);
            std::fclose(f);
        }
        wipe(text, 8193);
        std::free(text);
    }
    for (unsigned i = 0; i < c.provider_count; ++i) {
        auto &p = c.providers[i];
        if (!p.ca_file[0])
            continue;
        char path[512];
        int size = std::snprintf(path, sizeof(path), "%s%s", root_path, p.ca_file);
        FILE *f = size >= 0 && size < int(sizeof(path)) ? std::fopen(path, "rb") : nullptr;
        bool good = false;
        if (f) {
            size_t n = std::fread(p.ca_pem, 1, sizeof(p.ca_pem), f);
            good =
                !std::ferror(f) && n > 100 && n < sizeof(p.ca_pem) && !std::memchr(p.ca_pem, 0, n);
            std::fclose(f);
            if (good) {
                p.ca_pem[n] = 0;
                good = std::strstr(p.ca_pem, "-----BEGIN CERTIFICATE-----") &&
                       std::strstr(p.ca_pem, "-----END CERTIFICATE-----");
            }
        }
        if (!good) {
            wipe(p.ca_pem, sizeof(p.ca_pem));
            p.ready = false;
        }
    }
    if (!c.network.wifi_count)
        fallback(c, root_path);
}
} // namespace ou
// Reused shared Wi-Fi implementation's zeroisation hook, never credential logging.
namespace cm {
void wipe(void *p, size_t n) { ou::wipe(p, n); }
} // namespace cm
