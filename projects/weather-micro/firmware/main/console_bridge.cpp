#include "console_bridge.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include <cstdio>
#include <cstring>

namespace wm {
namespace {
const esp_partition_t *console_partition() {
    const auto *console = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                   ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    if (!console || console == esp_ota_get_running_partition() || console->address != 0x10000 ||
        console->size != 0x80000)
        return nullptr;
    esp_app_desc_t description{};
    if (esp_ota_get_partition_description(console, &description) != ESP_OK ||
        std::strncmp(description.project_name, "cyd_launcher", 32))
        return nullptr;
    return console;
}
} // namespace
bool console_available() { return console_partition() != nullptr; }
void confirm_first_boot(bool hardware_ok) {
    if (!hardware_ok || !console_available())
        return;
    esp_ota_img_states_t state{};
    const auto *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY)
        std::printf("WM_BOOT first_boot_confirm=%s\n",
                    esp_err_to_name(esp_ota_mark_app_valid_cancel_rollback()));
}
bool return_to_console() {
    const auto *console = console_partition();
    if (!console)
        return false;
    const auto result = esp_ota_set_boot_partition(console);
    std::printf("WM_CONSOLE return=%s nvs=PRESERVED\n", esp_err_to_name(result));
    std::fflush(stdout);
    if (result != ESP_OK)
        return false;
    esp_restart();
    return true;
}
} // namespace wm
