#include "json_guard.h"
#include "private_config.h"
#include "ui.h"
#include "usage_model.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
namespace {
const std::string legacy =
    R"({"included":{"session":{"remaining_percent":75,"resets_at":"2026-10-15T14:00:00Z"},"weekly":{"remaining_percent":10,"resets_at":"2026-10-18T13:00:00+00:00"}},"purchased":{"balance_usd":12.5}})";
const std::string credits =
    R"({"included":{"balance_usd":7.5,"allowance_usd":20,"period":{"from":"2026-10-01T00:00:00Z","until":"2026-11-01T00:00:00Z"}},"purchased":{"balance_usd":0}})";
const std::string usage =
    R"({"range":"24h","scope":"self","granularity":"hour","from":"2026-10-14T14:00:00Z","until":"2026-10-15T14:00:00Z","totals":{"request_count":53,"input_tokens":10000,"cached_input_tokens":2000,"output_tokens":1234,"usage_usd":1.25},"buckets":[{"from":"2026-10-14T14:00:00Z","until":"2026-10-14T15:00:00Z","request_count":33},{"from":"2026-10-14T15:00:00Z","until":"2026-10-14T16:00:00Z","request_count":20}]})";
void replace(std::string &s, const char *old, const char *value) {
    auto i = s.find(old);
    assert(i != s.npos);
    s.replace(i, std::strlen(old), value);
}
void model() {
    ou::Balance b;
    ou::Usage u;
    ou::Error e;
    assert(ou::parse_balance(legacy.data(), legacy.size(), b, e) && e == ou::Error::none);
    assert(b.kind == ou::BalanceKind::legacy && b.session_used == 25 && b.weekly_used == 90 &&
           b.purchased == 12.5);
    assert(ou::parse_balance(credits.data(), credits.size(), b, e) && b.percent_known &&
           b.used_percent == 62.5 && b.remaining == 7.5);
    auto zero = credits;
    replace(zero, "\"allowance_usd\":20", "\"allowance_usd\":0");
    assert(ou::parse_balance(zero.data(), zero.size(), b, e) && !b.percent_known);
    for (const char *bad : {"-1", "1e999", "\"20\"", "null"}) {
        auto s = credits;
        replace(s, "\"allowance_usd\":20", (std::string("\"allowance_usd\":") + bad).c_str());
        auto before = b;
        assert(!ou::parse_balance(s.data(), s.size(), b, e) && b.remaining == before.remaining);
    }
    for (const char *bad : {"101", "-1", "null"}) {
        auto s = legacy;
        replace(s, "\"remaining_percent\":75",
                (std::string("\"remaining_percent\":") + bad).c_str());
        assert(!ou::parse_balance(s.data(), s.size(), b, e));
    }
    assert(ou::parse_usage(usage.data(), usage.size(), u, e) && u.requests == 53 &&
           u.bucket_count == 2 && u.cost_known && u.cached_known && u.buckets[1] == 20);
    for (const char *bad : {"-1", "0.5", "9007199254740992", "null", "\"53\""}) {
        auto s = usage;
        replace(s, "\"request_count\":53", (std::string("\"request_count\":") + bad).c_str());
        assert(!ou::parse_usage(s.data(), s.size(), u, e) && u.requests == 53);
    }
    for (const auto &pair : std::vector<std::pair<const char *, const char *>>{
             {"\"scope\":\"self\"", "\"scope\":\"team\""},
             {"\"granularity\":\"hour\"", "\"granularity\":\"day\""},
             {"\"cached_input_tokens\":2000", "\"cached_input_tokens\":20000"},
             {"\"usage_usd\":1.25", "\"usage_usd\":-1"},
             {"2026-10-14T15:00:00Z\",\"until\":\"2026-10-14T16:00:00Z",
              "2026-10-14T14:00:00Z\",\"until\":\"2026-10-14T16:00:00Z"}}) {
        auto s = usage;
        replace(s, pair.first, pair.second);
        assert(!ou::parse_usage(s.data(), s.size(), u, e));
    }
    std::string minimal =
        R"({"range":"24h","scope":"self","granularity":"hour","from":"2026-10-14T00:00:00Z","until":"2026-10-15T00:00:00Z","totals":{"request_count":0},"buckets":[]})";
    assert(ou::parse_usage(minimal.data(), minimal.size(), u, e) && !u.cost_known &&
           !u.input_known && u.requests == 0);
    for (const std::string &s : std::vector<std::string>{
             "", "null", "{}x", legacy + "[]", std::string(24577, ' '),
             R"({"included":{},"included":{},"purchased":{}})",
             R"({"included\u0000":{},"purchased":{}})", std::string(legacy).insert(10, 1, '\0'),
             std::string(20, '[') + std::string(20, ']')})
        assert(!ou::parse_balance(s.data(), s.size(), b, e));
    auto overflow = credits;
    replace(overflow, "\"allowance_usd\":20", "\"allowance_usd\":1e-320");
    replace(overflow, "\"balance_usd\":7.5", "\"balance_usd\":-1e12");
    assert(!ou::parse_balance(overflow.data(), overflow.size(), b, e));
    auto truncated = legacy;
    for (size_t n = 0; n < truncated.size(); ++n)
        assert(!ou::parse_balance(truncated.data(), n, b, e));
    int64_t z = 0, offset = 0;
    assert(ou::parse_timestamp("1970-01-01T00:00:00Z", z) && z == 0);
    assert(ou::parse_timestamp("2024-02-29T00:00:00Z", z));
    assert(ou::parse_timestamp("2024-02-29T11:00:00.123+11:00", offset) && offset == z);
    for (const char *s : {"2023-02-29T00:00:00Z", "2024-13-01T00:00:00Z", "2024-01-00T00:00:00Z",
                          "2024-01-01T24:00:00Z", "2024-01-01T00:00:00", "2024-01-01T00:00:00Zxxx",
                          "2024-01-01T00:00:00+99:00"})
        assert(!ou::parse_timestamp(s, z));
    char text[64];
    ou::countdown(100, 1, text, sizeof(text));
    assert(std::string(text) == "Reset in 2m");
    ou::countdown(1, 100, text, sizeof(text));
    assert(std::string(text) == "Reset due");
    ou::countdown(100, 0, text, sizeof(text));
    assert(std::string(text) == "Reset unavailable");
    ou::PollPolicy p;
    assert(p.ready(0));
    p.begin(0);
    p.finish(1, true, 200);
    assert(!p.ready(60000) && p.ready(60001));
    assert(!p.manual(2));
    p.begin(60001);
    p.finish(60002, false, 429, 180);
    assert(p.due == 240002 && !p.manual(120002));
    p.finish(240003, false, 401);
    assert(p.blocked && !p.ready(9999999));
    p.manual(9999999);
    assert(!p.blocked && p.ready(9999999));
    for (int i = 0; i < 20; ++i)
        p.finish(0, false, 500);
    assert(p.due == 600000);
    assert(ou::retry_seconds("123") == 123 && ou::retry_seconds("-3") == 0 &&
           ou::retry_seconds("22s") == 0 &&
           ou::retry_seconds("9999999999999999999999999") == UINT32_MAX);
}
void config() {
    ou::Config c;
    std::string good =
        R"({"api_key":"public-test-key-123456","wifi":[{"ssid":"PUBLIC_TEST","password":"public-password","priority":2}]})";
    assert(ou::parse_private_config(good.data(), good.size(), c) && c.key_ready &&
           c.network.wifi_count == 1 && c.network.wifi[0].priority == 2);
    std::vector<std::string> bad = {R"({"api_key":1})",
                                    R"({"api_key":"one","api_key":"two"})",
                                    R"({"api_key":"1234567890123456\u0000suffix"})",
                                    R"({"api_key":"1234567890123456","wifi":{}})",
                                    good + "x",
                                    std::string(15, '[') + std::string(15, ']'),
                                    "{}\0garbage"};
    bad.back() = std::string("{}\0garbage", 10);
    for (const auto &s : bad) {
        assert(!ou::parse_private_config(s.data(), s.size(), c));
        assert(!c.key_ready && c.api_key[0] == 0);
    }
    for (const char *k : {"", "short", "YOUR_OLLAMA_KEY_1234", "newline\\ninvalid123456"}) {
        std::string s = "{\"api_key\":\"" + std::string(k) + "\"}";
        assert(ou::parse_private_config(s.data(), s.size(), c) && !c.key_ready);
    }
    std::string duplicate =
        R"({"api_key":"public-test-key-123456","wifi":[{"ssid":"TEST","password":"public-password"},{"ssid":"TEST","password":"public-password"},{"ssid":"OPEN","password":""}]})";
    assert(ou::parse_private_config(duplicate.data(), duplicate.size(), c) &&
           c.network.wifi_count == 1);
    // Nest and string escapes are bounded before calling recursive cJSON.
    assert(!ou::json_guard("{\"x\":\"\\u0000\"}", 14, 8));
    assert(ou::json_guard("{\"x\":\"\\\\u0000\"}", 15, 8));
}

void storage(const std::string &output) {
    namespace fs = std::filesystem;
    const auto root = fs::path(output) / "public-sd-fixture";
    for (const char *path : {"private", "file-server", "terminal-micro", "contextual-micro"})
        fs::create_directories(root / path);
    auto write = [&](const char *path, const std::string &text) {
        std::ofstream(root / path) << text;
    };
    const std::string network = R"({"ssid":"PUBLIC_TEST","password":"public-password"})";
    const std::string key = R"({"api_key":"public-test-key-123456"})";
    write("private/ollama.json", key);
    write("file-server/net-a.json", "{\"generation\":1,\"wifi\":[" + network + "]}");
    write("file-server/net-b.json",
          "{\"generation\":2,\"wifi\":[{\"ssid\":\"NEW_TEST\",\"password\":\"public-password\"}]}");
    ou::Config c;
    ou::load_private_config(c, root.c_str());
    assert(c.key_ready && c.network.wifi_count == 1 &&
           std::string(c.network.wifi[0].ssid) == "NEW_TEST");
    write("file-server/net-b.json", std::string(20, '[') + std::string(20, ']'));
    ou::load_private_config(c, root.c_str());
    assert(c.key_ready && std::string(c.network.wifi[0].ssid) == "PUBLIC_TEST");
    write("private/ollama.json", "{\"api_key\":\"public-test-key-123456\",\"wifi\":[{\"ssid\":"
                                 "\"OVERRIDE\",\"password\":\"public-password\"}]}");
    ou::load_private_config(c, root.c_str());
    assert(c.key_ready && c.network.wifi_count == 1 &&
           std::string(c.network.wifi[0].ssid) == "OVERRIDE");
    fs::remove(root / "private/ollama.json");
    ou::load_private_config(c, root.c_str());
    assert(!c.key_ready && c.api_key[0] == 0 && c.network.wifi_count == 1);
    fs::remove(root / "file-server/net-a.json");
    fs::remove(root / "file-server/net-b.json");
    write("terminal-micro/config.json", "{\"wifi\":[" + network + "]}");
    ou::load_private_config(c, root.c_str());
    assert(!c.key_ready && c.network.wifi_count == 1);
    std::ifstream preserved(root / "terminal-micro/config.json");
    std::string text((std::istreambuf_iterator<char>(preserved)), {});
    assert(text == "{\"wifi\":[" + network + "]}");
    fs::remove_all(root);
}
void capture(const std::string &name, ou::View &v) {
    ou::prepare(v);
    std::ofstream out(name, std::ios::binary);
    out << "P6\n320 240\n255\n";
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x) {
            auto c = ou::pixel(x, y, &v);
            unsigned char bytes[] = {static_cast<unsigned char>(((c >> 11) & 31) * 255 / 31),
                                     static_cast<unsigned char>(((c >> 5) & 63) * 255 / 63),
                                     static_cast<unsigned char>((c & 31) * 255 / 31)};
            out.write(reinterpret_cast<char *>(bytes), 3);
        }
}
void ui(const std::string &output) {
    ou::View v;
    ou::Error e;
    ou::parse_balance(legacy.data(), legacy.size(), v.balance, e);
    ou::parse_usage(usage.data(), usage.size(), v.usage, e);
    v.key = true;
    v.has_balance = true;
    v.has_usage = true;
    v.connected = true;
    v.networks = 2;
    ou::parse_timestamp("2026-10-15T12:00:00Z", v.epoch);
    std::strcpy(v.status, "Updated 20s ago / next 40s");
    std::strcpy(v.time, "23:00");
    for (bool light : {false, true}) {
        v.light = light;
        std::strcpy(v.status, "Updated 20s ago / next 40s");
        std::string prefix = output + (light ? "/light-" : "/dark-");
        v.page = ou::Page::balance;
        capture(prefix + "legacy.ppm", v);
        ou::parse_balance(credits.data(), credits.size(), v.balance, e);
        capture(prefix + "credits.ppm", v);
        v.page = ou::Page::activity;
        v.usage.bucket_count = 25;
        for (unsigned i = 0; i < 25; ++i)
            v.usage.buckets[i] = (i * 17) % 53;
        capture(prefix + "activity.ppm", v);
        v.page = ou::Page::network;
        capture(prefix + "network.ppm", v);
        v.has_balance = false;
        v.key = false;
        v.page = ou::Page::balance;
        std::strcpy(v.status, "Add API key / tap NET");
        capture(prefix + "setup.ppm", v);
        v.key = true;
        v.has_balance = true;
        v.stale_balance = true;
        std::strcpy(v.status, "HTTP 429 / retry delayed");
        capture(prefix + "stale.ppm", v);
        ou::parse_balance(legacy.data(), legacy.size(), v.balance, e);
        v.stale_balance = false;
    }
    for (auto page : {ou::Page::balance, ou::Page::activity, ou::Page::network}) {
        v.page = page;
        ou::Button b[8];
        auto n = ou::buttons(v, b, 8);
        assert(n == (page == ou::Page::network ? 8u : 5u));
        for (unsigned i = 0; i < n; ++i) {
            auto r = b[i].rect;
            assert(r.x >= 0 && r.y >= 0 && r.x + r.w <= 320 && r.y + r.h <= 240 && r.h >= 32);
            assert(ou::hit(v, r.x + r.w / 2, r.y + r.h / 2) == b[i].action);
            for (unsigned j = i + 1; j < n; ++j) {
                auto q = b[j].rect;
                assert(r.x + r.w <= q.x || q.x + q.w <= r.x || r.y + r.h <= q.y ||
                       q.y + q.h <= r.y);
            }
        }
        assert(ou::hit(v, -1, -1) == ou::Action::none && ou::hit(v, 320, 240) == ou::Action::none);
    }
    v.page = ou::Page::network;
    v.busy = true;
    assert(ou::hit(v, 40, 225) == ou::Action::none && ou::hit(v, 30, 180) == ou::Action::none);
    assert(ou::hit(v, 270, 225) == ou::Action::console &&
           ou::hit(v, 180, 225) == ou::Action::theme);
    v.busy = false;
    // Rebuilding a shorter scene must reproduce a fresh frame, with no old bars.
    v.page = ou::Page::balance;
    v.has_balance = false;
    ou::prepare(v);
    std::vector<uint16_t> baseline;
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x)
            baseline.push_back(ou::pixel(x, y, &v));
    v.page = ou::Page::activity;
    ou::prepare(v);
    v.page = ou::Page::balance;
    ou::prepare(v);
    size_t i = 0;
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x)
            assert(ou::pixel(x, y, &v) == baseline[i++]);
    assert(ou::text_width("OLLAMA USAGE", 2) <= 217);
    for (const char *label : {"REFRESH", "FETCHING", "CONSOLE", "RELOAD KEY"})
        assert(ou::text_width(label, 0) <= 68);
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--private-config") {
        std::string s((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
        ou::Config c;
        bool ok = ou::parse_private_config(s.data(), s.size(), c) && c.key_ready;
        ou::wipe(&c, sizeof(c));
        std::printf("PRIVATE_CONFIG valid_key=%d values=WITHHELD\n", ok);
        return ok ? 0 : 1;
    }
    if (argc >= 3 && std::string(argv[1]) == "--parse") {
        std::string s((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
        ou::Balance b;
        ou::Usage u;
        ou::Error e;
        bool ok = std::string(argv[2]) == "balance" ? ou::parse_balance(s.data(), s.size(), b, e)
                                                    : ou::parse_usage(s.data(), s.size(), u, e);
        std::printf("LIVE_PARSE %s valid=%d error=%s\n", argv[2], ok, ou::error_name(e));
        return ok ? 0 : 1;
    }
    assert(argc == 2);
    model();
    config();
    storage(argv[1]);
    ui(argv[1]);
    std::puts("OU_NATIVE model/config/poll-policy/UI PASS (ASan/UBSan)");
}
