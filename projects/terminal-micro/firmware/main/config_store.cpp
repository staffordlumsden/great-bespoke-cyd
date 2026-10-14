#include "config_store.h"
#include "cJSON.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

namespace cm {
void wipe(void* p, size_t n) { auto* b = static_cast<volatile unsigned char*>(p); while (n--) *b++ = 0; }
namespace {
bool fail(ConfigError& e, ConfigCode code, ConfigField field) { e = {code, field}; return false; }
bool utf8(const char* data, size_t n, bool no_controls) {
    const auto* s = reinterpret_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n;) {
        uint32_t cp = s[i++]; unsigned count = 0; uint32_t minimum = 0;
        if (cp < 128) { if (!cp || (no_controls && (cp < 32 || cp == 127))) return false; continue; }
        if (cp >= 0xc2 && cp <= 0xdf) { count = 1; minimum = 0x80; cp &= 31; }
        else if (cp >= 0xe0 && cp <= 0xef) { count = 2; minimum = 0x800; cp &= 15; }
        else if (cp >= 0xf0 && cp <= 0xf4) { count = 3; minimum = 0x10000; cp &= 7; }
        else return false;
        if (i + count > n) return false;
        while (count--) { unsigned c = s[i++]; if ((c & 0xc0) != 0x80) return false; cp = (cp << 6) | (c & 63); }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
            (no_controls && cp >= 0x80 && cp <= 0x9f)) return false;
    }
    return true;
}
bool preflight(const char* s, size_t n, ConfigError& e) {
    if (!utf8(s, n, false)) return fail(e, ConfigCode::encoding, ConfigField::root);
    unsigned depth = 0; bool quoted = false;
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (quoted && c == '\\') {
            if (i + 5 < n && s[i+1] == 'u' && std::memcmp(s+i+2, "0000", 4) == 0)
                return fail(e, ConfigCode::encoding, ConfigField::root);
            if (++i >= n) return fail(e, ConfigCode::syntax, ConfigField::root);
        } else if (c == '"') quoted = !quoted;
        else if (!quoted && (c == '{' || c == '[')) {
            if (++depth > 8) return fail(e, ConfigCode::depth, ConfigField::root);
        } else if (!quoted && (c == '}' || c == ']')) {
            if (!depth) return fail(e, ConfigCode::syntax, ConfigField::root);
            --depth;
        }
    }
    return true; // cJSON checks unmatched delimiters and complete syntax.
}
bool walk(const cJSON* node, ConfigError& e) {
    if (cJSON_IsString(node) && (!node->valuestring || !utf8(node->valuestring, std::strlen(node->valuestring), true)))
        return fail(e, ConfigCode::encoding, ConfigField::root);
    for (const cJSON* a = node->child; a; a = a->next) {
        if (cJSON_IsObject(node)) {
            if (!a->string || !utf8(a->string, std::strlen(a->string), true)) return fail(e, ConfigCode::encoding, ConfigField::root);
            for (const cJSON* b = a->next; b; b = b->next)
                if (b->string && std::strcmp(a->string, b->string) == 0) return fail(e, ConfigCode::duplicate, ConfigField::root);
        }
        if (!walk(a, e)) return false;
    }
    return true;
}
void wipe_tree(cJSON* p) {
    for (cJSON* c = p->child; c; c = c->next) wipe_tree(c);
    if (p->valuestring) wipe(p->valuestring, std::strlen(p->valuestring));
    if (p->string) wipe(p->string, std::strlen(p->string));
}
bool object(const cJSON* o, const char* const* keys, size_t n, ConfigError& e, ConfigField field) {
    if (!cJSON_IsObject(o)) return fail(e, ConfigCode::type, field);
    for (const cJSON* p = o->child; p; p = p->next) {
        bool known = false;
        for (size_t i = 0; i < n; ++i) if (std::strcmp(p->string, keys[i]) == 0) known = true;
        if (!known) return fail(e, ConfigCode::unknown, field);
    }
    return true;
}
const cJSON* get(const cJSON* o, const char* key, ConfigError& e, ConfigField field) {
    const cJSON* p = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!p) fail(e, ConfigCode::missing, field);
    return p;
}
bool str(const cJSON* o, const char* key, char* dst, size_t cap, ConfigError& e, ConfigField field) {
    const auto* p = get(o, key, e, field); if (!p) return false;
    if (!cJSON_IsString(p)) return fail(e, ConfigCode::type, field);
    const size_t n = std::strlen(p->valuestring);
    if (!n || n >= cap) return fail(e, ConfigCode::range, field);
    std::memcpy(dst, p->valuestring, n+1); return true;
}
bool integer(const cJSON* o, const char* key, int lo, int hi, int& v, ConfigError& e, ConfigField field) {
    const auto* p = get(o, key, e, field); if (!p) return false;
    if (!cJSON_IsNumber(p)) return fail(e, ConfigCode::type, field);
    const double x = p->valuedouble;
    if (!std::isfinite(x) || x < lo || x > hi || std::floor(x) != x) return fail(e, ConfigCode::range, field);
    v = static_cast<int>(x); return true;
}
bool boolean(const cJSON* o, const char* key, bool& v, ConfigError& e) {
    const auto* p = get(o, key, e, ConfigField::ui); if (!p) return false;
    if (!cJSON_IsBool(p)) return fail(e, ConfigCode::type, ConfigField::ui);
    v = cJSON_IsTrue(p); return true;
}
bool ident(const char* s) {
    for (; *s; ++s) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
        (*s >= '0' && *s <= '9') || *s == '-' || *s == '_')) return false;
    return true;
}
bool hostname(const char* s) {
    bool numeric = true; for (const char* p = s; *p; ++p) if ((*p < '0' || *p > '9') && *p != '.') numeric = false;
    if (numeric) {
        unsigned parts = 0;
        while (*s) {
            const char* start = s; unsigned v = 0;
            while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); if (v > 255) return false; }
            if (s == start || (s-start > 1 && *start == '0')) return false;
            if (++parts > 4 || (*s && *s++ != '.')) return false;
            if (!*s) return parts == 4 && s[-1] != '.';
        }
        return false;
    }
    size_t count = 0; bool first = true; char last = 0;
    for (; *s; ++s) {
        if (*s == '.') { if (first || last == '-' || count > 63) return false; first = true; count = 0; }
        else {
            const bool alnum = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9');
            if ((!alnum && *s != '-') || (first && *s == '-')) return false;
            first = false; if (++count > 63) return false;
        }
        last = *s;
    }
    return !first && last != '-';
}
int b64(char c) {
    if (c >= 'A' && c <= 'Z') return c-'A';
    if (c >= 'a' && c <= 'z') return c-'a'+26;
    if (c >= '0' && c <= '9') return c-'0'+52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}
bool fingerprint(const char* s, uint8_t* out) {
    if (std::strlen(s) != 50 || std::memcmp(s, "SHA256:", 7)) return false;
    uint32_t acc = 0; unsigned bits = 0, pos = 0;
    for (s += 7; *s; ++s) {
        const int v = b64(*s); if (v < 0) return false;
        acc = (acc << 6) | static_cast<unsigned>(v); bits += 6;
        if (bits >= 8) { bits -= 8; if (pos >= 32) return false; out[pos++] = static_cast<uint8_t>(acc >> bits); }
    }
    return pos == 32 && bits == 2 && (acc & 3) == 0;
}
bool validate(const cJSON* root, AppConfig& cfg, ConfigError& e) {
    constexpr const char* root_keys[] = {"schema_version","device_name","wifi","ssh_hosts","ui"};
    if (!object(root, root_keys, 5, e, ConfigField::root)) return false;
    int version = 0;
    if (!integer(root,"schema_version",1,1,version,e,ConfigField::schema) ||
        !str(root,"device_name",cfg.device_name,sizeof(cfg.device_name),e,ConfigField::device)) return false;
    if (!ident(cfg.device_name)) return fail(e,ConfigCode::range,ConfigField::device);
    const auto* wifi = get(root,"wifi",e,ConfigField::wifi); if (!wifi) return false;
    if (!cJSON_IsArray(wifi)) return fail(e,ConfigCode::type,ConfigField::wifi);
    const int nw = cJSON_GetArraySize(wifi);
    if (nw < 1 || nw > static_cast<int>(max_wifi)) return fail(e,ConfigCode::range,ConfigField::wifi);
    cfg.wifi_count = nw;
    constexpr const char* wifi_keys[] = {"ssid","password","priority"};
    for (int i = 0; i < nw; ++i) {
        const auto* o = cJSON_GetArrayItem(wifi,i); auto& w = cfg.wifi[i];
        if (!object(o,wifi_keys,3,e,ConfigField::wifi) ||
            !str(o,"ssid",w.ssid,sizeof(w.ssid),e,ConfigField::ssid) ||
            !str(o,"password",w.password,sizeof(w.password),e,ConfigField::password) ||
            !integer(o,"priority",-1000,1000,w.priority,e,ConfigField::priority)) return false;
        if (!std::strcmp(w.ssid,"YOUR_2_4_GHZ_NETWORK") || !std::strcmp(w.ssid,"YOUR_SECOND_NETWORK") ||
            !std::strcmp(w.password,"REPLACE_ON_PRIVATE_SD_CARD")) return fail(e,ConfigCode::placeholder,ConfigField::wifi);
        const size_t n = std::strlen(w.password);
        if (n < 8) return fail(e,ConfigCode::range,ConfigField::password);
        if (n == 64) for (size_t j=0;j<n;++j) if (!((w.password[j]>='0'&&w.password[j]<='9') ||
            (w.password[j]>='a'&&w.password[j]<='f') || (w.password[j]>='A'&&w.password[j]<='F')))
                return fail(e,ConfigCode::range,ConfigField::password);
        for (int j=0;j<i;++j) if (!std::strcmp(w.ssid,cfg.wifi[j].ssid)) return fail(e,ConfigCode::duplicate,ConfigField::ssid);
    }
    const auto* hosts = get(root,"ssh_hosts",e,ConfigField::hosts); if (!hosts) return false;
    if (!cJSON_IsArray(hosts)) return fail(e,ConfigCode::type,ConfigField::hosts);
    const int nh = cJSON_GetArraySize(hosts);
    if (nh < 0 || nh > static_cast<int>(max_hosts)) return fail(e,ConfigCode::range,ConfigField::hosts);
    cfg.host_count = nh;
    constexpr const char* host_keys[] = {"id","label","host","port","username","auth","known_host","terminal_type","startup_command"};
    constexpr const char* auth_keys[] = {"method","private_key_file","passphrase_source"};
    constexpr const char* pin_keys[] = {"verification","sha256_fingerprint"};
    for (int i=0;i<nh;++i) {
        const auto* o = cJSON_GetArrayItem(hosts,i); auto& h = cfg.hosts[i]; int port=0;
        if (!object(o,host_keys,9,e,ConfigField::hosts) || !str(o,"id",h.id,sizeof(h.id),e,ConfigField::id) ||
            !str(o,"label",h.label,sizeof(h.label),e,ConfigField::label) || !str(o,"host",h.host,sizeof(h.host),e,ConfigField::address) ||
            !integer(o,"port",1,65535,port,e,ConfigField::port) || !str(o,"username",h.username,sizeof(h.username),e,ConfigField::username) ||
            !str(o,"terminal_type",h.terminal_type,sizeof(h.terminal_type),e,ConfigField::terminal) ||
            !str(o,"startup_command",h.startup_command,sizeof(h.startup_command),e,ConfigField::command)) return false;
        h.port = static_cast<uint16_t>(port);
        if (!ident(h.id)) return fail(e,ConfigCode::range,ConfigField::id);
        if (!hostname(h.host)) return fail(e,ConfigCode::range,ConfigField::address);
        if (std::strcmp(h.terminal_type,"xterm") && std::strcmp(h.terminal_type,"vt100")) return fail(e,ConfigCode::unsupported,ConfigField::terminal);
        for (int j=0;j<i;++j) if (!std::strcmp(h.id,cfg.hosts[j].id)) return fail(e,ConfigCode::duplicate,ConfigField::id);
        const auto* a = get(o,"auth",e,ConfigField::auth); if (!a) return false;
        char method[17]{}, source[17]{};
        if (!object(a,auth_keys,3,e,ConfigField::auth) || !str(a,"method",method,sizeof(method),e,ConfigField::auth) ||
            !str(a,"passphrase_source",source,sizeof(source),e,ConfigField::auth) ||
            !str(a,"private_key_file",h.private_key_file,sizeof(h.private_key_file),e,ConfigField::key_file)) return false;
        if (std::strcmp(method,"publickey") || std::strcmp(source,"none")) return fail(e,ConfigCode::unsupported,ConfigField::auth);
        constexpr char prefix[] = "/contextual-micro/ssh_keys/";
        if (std::strncmp(h.private_key_file,prefix,sizeof(prefix)-1)) return fail(e,ConfigCode::range,ConfigField::key_file);
        const char* leaf = h.private_key_file+sizeof(prefix)-1;
        if (!*leaf || !std::strcmp(leaf,".") || !std::strcmp(leaf,"..")) return fail(e,ConfigCode::range,ConfigField::key_file);
        for (const char* p=leaf;*p;++p) if (!((*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') || (*p>='0'&&*p<='9') ||
            *p=='_' || *p=='-' || *p=='.')) return fail(e,ConfigCode::range,ConfigField::key_file);
        const auto* p = get(o,"known_host",e,ConfigField::pin); if (!p) return false;
        char verification[17]{}, pin[52]{};
        if (!object(p,pin_keys,2,e,ConfigField::pin) || !str(p,"verification",verification,sizeof(verification),e,ConfigField::pin) ||
            !str(p,"sha256_fingerprint",pin,sizeof(pin),e,ConfigField::pin)) return false;
        if (std::strcmp(verification,"pinned") || !fingerprint(pin,h.host_sha256)) return fail(e,ConfigCode::range,ConfigField::pin);
    }
    constexpr const char* ui_keys[] = {"theme","auto_connect_default_host","tailscale_visible","tailscale_enabled"};
    const auto* ui = get(root,"ui",e,ConfigField::ui); if (!ui) return false;
    char theme[17]{}; bool visible=false,enabled=true;
    if (!object(ui,ui_keys,4,e,ConfigField::ui) || !str(ui,"theme",theme,sizeof(theme),e,ConfigField::ui) ||
        !boolean(ui,"auto_connect_default_host",cfg.auto_connect_default_host,e) || !boolean(ui,"tailscale_visible",visible,e) ||
        !boolean(ui,"tailscale_enabled",enabled,e)) return false;
    if (std::strcmp(theme,"contextual") || !visible || enabled || (!nh && cfg.auto_connect_default_host))
        return fail(e,ConfigCode::unsupported,ConfigField::ui);
    return true;
}
}
const char* config_code_name(ConfigCode c) {
    constexpr const char* names[] = {"OK","MISSING_FILE","IO","SIZE","ENCODING","DEPTH","SYNTAX","DUPLICATE","UNKNOWN_FIELD","MISSING_FIELD","TYPE","RANGE","UNSUPPORTED","PLACEHOLDER","MEMORY"};
    const unsigned i=static_cast<unsigned>(c); return i<sizeof(names)/sizeof(*names) ? names[i] : "UNKNOWN";
}
const char* config_field_name(ConfigField f) {
    constexpr const char* names[] = {"ROOT","SCHEMA","DEVICE","WIFI","SSID","PASSWORD","PRIORITY","HOSTS","ID","LABEL","ADDRESS","PORT","USERNAME","AUTH","KEY_FILE","HOST_PIN","TERMINAL","COMMAND","UI"};
    const unsigned i=static_cast<unsigned>(f); return i<sizeof(names)/sizeof(*names) ? names[i] : "UNKNOWN";
}
bool parse_config(const char* json, size_t bytes, AppConfig& out, ConfigError& e) {
    e = {};
    if (!json || !bytes || bytes > max_config_bytes) return fail(e,ConfigCode::size,ConfigField::root);
    if (!preflight(json,bytes,e)) return false;
    char* copy=static_cast<char*>(std::malloc(bytes+1));
    auto* cfg=new(std::nothrow) AppConfig{};
    if (!copy || !cfg) { std::free(copy); delete cfg; return fail(e,ConfigCode::memory,ConfigField::root); }
    std::memcpy(copy,json,bytes); copy[bytes]=0;
    const char* end=nullptr;
    cJSON* root=cJSON_ParseWithLengthOpts(copy,bytes+1,&end,1);
    bool valid=false;
    if (!root) fail(e,ConfigCode::syntax,ConfigField::root);
    else { valid=walk(root,e) && validate(root,*cfg,e); wipe_tree(root); cJSON_Delete(root); }
    wipe(copy,bytes+1); std::free(copy);
    if (valid) { wipe(&out,sizeof(out)); out=*cfg; }
    wipe(cfg,sizeof(*cfg)); delete cfg;
    return valid;
}
bool load_sd_config(AppConfig& out, ConfigError& e) {
    FILE* f=std::fopen("/sdcard/terminal-micro/config.json","rb");
    if (!f) f=std::fopen("/sdcard/contextual-micro/config.json","rb");
    if (!f) return fail(e,ConfigCode::missing_file,ConfigField::root);
    char* data=static_cast<char*>(std::malloc(max_config_bytes+1));
    if (!data) { std::fclose(f); return fail(e,ConfigCode::memory,ConfigField::root); }
    const size_t n=std::fread(data,1,max_config_bytes+1,f);
    const bool io=std::ferror(f); const int closed=std::fclose(f);
    bool valid=false;
    if (io || closed) fail(e,ConfigCode::io,ConfigField::root);
    else valid=parse_config(data,n,out,e);
    wipe(data,max_config_bytes+1); std::free(data); return valid;
}
}
