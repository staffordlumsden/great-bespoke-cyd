#pragma once
#include <cstddef>
#include <cstdint>

namespace cm {
constexpr size_t max_config_bytes = 8192, max_wifi = 4, max_hosts = 8;
struct WifiNetwork { char ssid[33]{}; char password[65]{}; int priority = 0; };
struct HostProfile {
    char id[33]{}, label[49]{}, host[254]{}, username[65]{};
    char private_key_file[129]{}, terminal_type[17]{}, startup_command[257]{};
    uint16_t port = 22;
    uint8_t host_sha256[32]{};
};
struct AppConfig {
    char device_name[33]{};
    WifiNetwork wifi[max_wifi]{};
    HostProfile hosts[max_hosts]{};
    size_t wifi_count = 0, host_count = 0;
    bool auto_connect_default_host = false;
};
enum class ConfigCode { ok, missing_file, io, size, encoding, depth, syntax,
    duplicate, unknown, missing, type, range, unsupported, placeholder, memory };
enum class ConfigField { root, schema, device, wifi, ssid, password, priority,
    hosts, id, label, address, port, username, auth, key_file, pin, terminal,
    command, ui };
struct ConfigError { ConfigCode code = ConfigCode::ok; ConfigField field = ConfigField::root; };
const char* config_code_name(ConfigCode);
const char* config_field_name(ConfigField);
void wipe(void*, size_t);
// Transactional: output is unchanged on failure. No input values are logged.
bool parse_config(const char*, size_t, AppConfig&, ConfigError&);
bool load_sd_config(AppConfig&, ConfigError&);
}
