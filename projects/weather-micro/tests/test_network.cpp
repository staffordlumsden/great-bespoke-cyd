#include "network_config.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
namespace fs = std::filesystem;
void write(const fs::path &p, const char *text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    fs::path root = argv[1];
    cm::AppConfig config;
    write(root / "file-server/net-a.json",
          R"({"generation":1,"wifi":[{"ssid":"Fixture A","password":"fixture-pass-123"}]})");
    write(root / "file-server/net-b.json",
          R"({"generation":2,"wifi":[{"ssid":"Fixture B","password":"fixture-pass-456"}]})");
    wm::load_networks(config, root.c_str());
    assert(config.wifi_count == 1 && !std::strcmp(config.wifi[0].ssid, "Fixture B"));
    write(root / "file-server/net-b.json",
          R"({"generation":3,"wifi":[{"ssid":"Invalid","password":"short"}]})");
    wm::load_networks(config, root.c_str());
    assert(config.wifi_count == 1 && !std::strcmp(config.wifi[0].ssid, "Fixture A"));
    write(
        root / "terminal-micro/config.json",
        R"({"wifi":[{"ssid":"Fixture A","password":"fixture-pass-123"},{"ssid":"Fixture C","password":"fixture-pass-789","priority":10}]})");
    wm::load_networks(config, root.c_str());
    assert(config.wifi_count == 2 && config.wifi[1].priority == 10);
    fs::remove_all(root / "file-server");
    write(root / "terminal-micro/config.json",
          R"({"wifi":[],"wifi":[{"ssid":"Fixture A","password":"fixture-pass-123"}]})");
    wm::load_networks(config, root.c_str());
    assert(config.wifi_count == 0);
    std::puts("PASS read-only Wi-Fi profile precedence, fallback, deduplication, password checks "
              "and duplicate JSON rejection; fixture values withheld");
}
