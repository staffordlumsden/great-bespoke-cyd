#pragma once
#include "config_store.h"
#include "usage_model.h"
namespace ou {
struct ProviderConfig {
    ProviderKind kind = ProviderKind::ollama;
    char id[17]{}, label[17]{}, api_key[513]{}, url[257]{}, ca_file[81]{}, ca_pem[2049]{};
    bool ready = false;
};
struct Config {
    ProviderConfig providers[max_providers]{};
    unsigned provider_count = 0;

    char api_key[513]{};
    cm::AppConfig network{};
    bool file_loaded = false, key_ready = false;
};
// Never logs values or writes credentials to NVS. Output replaces previous secrets.
bool parse_private_config(const char *, size_t, Config &);
void load_private_config(Config &, const char *sd_root = "/sdcard");
void wipe(void *, size_t);
} // namespace ou
