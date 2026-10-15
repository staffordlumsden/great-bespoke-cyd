#pragma once
#include "config_store.h"
namespace ou {
struct Config {
    char api_key[513]{};
    cm::AppConfig network{};
    bool file_loaded = false, key_ready = false;
};
// Never logs values or writes credentials to NVS. Output replaces previous secrets.
bool parse_private_config(const char *, size_t, Config &);
void load_private_config(Config &, const char *sd_root = "/sdcard");
void wipe(void *, size_t);
} // namespace ou
