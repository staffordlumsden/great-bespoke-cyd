#include "private_config.h"
#include "ui.h"
#include "usage_model.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
namespace {
const char *feed =
    R"({"schema":1,"provider":"codex","updated_at":1792108800,"meters":[{"label":"5H WINDOW","used_percent":12,"resets_at":1792126800},{"label":"WEEKLY","used_percent":34,"resets_at":1792537200}],"stats":[]})";
const char *router =
    R"({"data":{"limit":20,"limit_remaining":7.5,"usage":30,"usage_daily":1.25,"usage_weekly":4.2}})";
void capture(ou::View &v, const std::string &path) {
    ou::prepare(v);
    std::ofstream f(path, std::ios::binary);
    f << "P6\n320 240\n255\n";
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x) {
            auto c = ou::pixel(x, y, nullptr);
            unsigned char rgb[] = {static_cast<unsigned char>(((c >> 11) & 31) * 255 / 31),
                                   static_cast<unsigned char>(((c >> 5) & 63) * 255 / 63),
                                   static_cast<unsigned char>((c & 31) * 255 / 31)};
            f.write(reinterpret_cast<char *>(rgb), 3);
        }
}
void models() {
    ou::Metrics m;
    ou::Error e;
    assert(ou::parse_feed(feed, std::strlen(feed), "codex", m, e) && m.meter_count == 2 &&
           m.meters[0].used == 12 && m.stats[0].value[0] == 0);
    assert(ou::feed_is_fresh(m, m.updated_at) && ou::feed_is_fresh(m, m.updated_at + 180));
    assert(!ou::feed_is_fresh(m, m.updated_at + 181) && !ou::feed_is_fresh(m, m.updated_at - 121));
    assert(!ou::parse_feed(feed, std::strlen(feed), "other", m, e));
    for (const auto &pair :
         {std::pair<const char *, const char *>{"\"used_percent\":12", "\"used_percent\":101"},
          {"\"schema\":1", "\"schema\":2"},
          {"\"updated_at\":1792108800", "\"updated_at\":0"},
          {"\"label\":\"5H WINDOW\"", "\"label\":\"too long label for a screen\""},
          {"\"stats\":[]", "\"stats\":[{\"label\":\"CREDITS\",\"value\":\"bad\\nvalue\"}]"}}) {
        std::string s = feed;
        auto pos = s.find(pair.first);
        assert(pos != s.npos);
        s.replace(pos, std::strlen(pair.first), pair.second);
        assert(!ou::parse_feed(s.data(), s.size(), "codex", m, e));
    }
    assert(ou::parse_openrouter(router, std::strlen(router), m, e) && m.meter_count == 1 &&
           m.meters[0].used == 62.5 && m.stat_count == 3);
    std::string unknown = R"({"data":{"limit":null,"limit_remaining":null,"usage":0}})";
    assert(ou::parse_openrouter(unknown.data(), unknown.size(), m, e) && m.meter_count == 0 &&
           m.stat_count == 1 && std::string(m.stats[0].label) == "TOTAL KEY SPEND");
    for (const char *bad : {"{}", "{\"data\":{}}", "{\"data\":{\"usage\":-1}}",
                            "{\"data\":{\"usage\":0,\"usage_daily\":-1}}"})
        assert(!ou::parse_openrouter(bad, std::strlen(bad), m, e));
}
void config() {
    ou::Config c;
    std::string valid =
        R"({"providers":[{"type":"ollama","api_key":"public-test-key-123456"},{"type":"codex","url":"https://192.0.2.1:8769/v1/providers/codex","api_key":"public-test-key-123456","ca_file":"/private/usage-ca.pem"},{"type":"openrouter","api_key":"public-test-key-123456"},{"type":"feed","id":"my-ai","label":"My AI","url":"https://example.com/v1/providers/my-ai","api_key":"public-test-key-123456"}]})";
    assert(ou::parse_private_config(valid.data(), valid.size(), c) && c.provider_count == 4 &&
           c.providers[0].ready && c.providers[1].kind == ou::ProviderKind::codex &&
           c.providers[2].kind == ou::ProviderKind::openrouter);
    for (const char *url : {"http://192.0.2.1/a", "https://user@example.com/a",
                            "https://example.com:999999/a", "https://example.com/a?token=secret",
                            "https://example.com/../secret", "https://example.com"}) {
        std::string s =
            "{\"providers\":[{\"type\":\"feed\",\"url\":\"" + std::string(url) + "\"}]}";
        assert(!ou::parse_private_config(s.data(), s.size(), c));
    }
    std::string duplicate = R"({"providers":[{"type":"ollama"},{"type":"ollama"}]})";
    assert(!ou::parse_private_config(duplicate.data(), duplicate.size(), c));
    std::string missing = R"({"providers":[{"type":"openrouter"}]})";
    assert(ou::parse_private_config(missing.data(), missing.size(), c) && c.provider_count == 1 &&
           !c.providers[0].ready);
    std::string ambiguous = R"({"api_key":"public-test-key-123456","providers":[]})";
    assert(!ou::parse_private_config(ambiguous.data(), ambiguous.size(), c));
}
void ui(const std::string &output) {
    ou::View v;
    v.provider_count = 4;
    v.key = true;
    v.has_balance = true;
    v.connected = true;
    v.networks = 1;
    std::strcpy(v.status, "Updated 10s ago / tap source name");
    v.epoch = 1792108800;
    const char *labels[] = {"Ollama", "Codex", "OpenRouter", "My AI"};
    for (unsigned i = 0; i < 4; ++i) {
        std::strcpy(v.provider_names[i], labels[i]);
        v.provider_ready[i] = true;
    }
    for (bool light : {false, true}) {
        v.light = light;
        std::string prefix = output + (light ? "/light-" : "/dark-");
        ou::Error e;
        v.selected_provider = 1;
        v.kind = ou::ProviderKind::codex;
        std::strcpy(v.provider_name, "Codex");
        v.page = ou::Page::balance;
        ou::parse_feed(feed, std::strlen(feed), "codex", v.metrics, e);
        capture(v, prefix + "codex.ppm");
        v.selected_provider = 2;
        v.kind = ou::ProviderKind::openrouter;
        std::strcpy(v.provider_name, "OpenRouter");
        ou::parse_openrouter(router, std::strlen(router), v.metrics, e);
        capture(v, prefix + "openrouter.ppm");
        v.page = ou::Page::sources;
        capture(v, prefix + "sources.ppm");
        v.page = ou::Page::add;
        capture(v, prefix + "add.ppm");
        v.page = ou::Page::balance;
        v.kind = ou::ProviderKind::feed;
        std::strcpy(v.provider_name, "My AI");
        v.stale_balance = true;
        capture(v, prefix + "custom-stale.ppm");
        v.stale_balance = false;
    }
    for (auto page : {ou::Page::balance, ou::Page::network, ou::Page::sources, ou::Page::add}) {
        v.page = page;
        ou::Button b[12];
        unsigned n = ou::buttons(v, b, 12);
        for (unsigned i = 0; i < n; ++i) {
            auto r = b[i].rect;
            assert(r.h >= 34 && r.x >= 0 && r.y >= 0 && r.x + r.w <= 320 && r.y + r.h <= 240);
            for (unsigned j = i + 1; j < n; ++j) {
                auto q = b[j].rect;
                assert(r.x + r.w <= q.x || q.x + q.w <= r.x || r.y + r.h <= q.y ||
                       q.y + q.h <= r.y);
            }
        }
    }
    v.page = ou::Page::sources;
    for (unsigned i = 0; i < 4; ++i)
        assert(ou::hit(v, 160, 60 + int(i) * 39) ==
               static_cast<ou::Action>(int(ou::Action::source0) + i));
    v.provider_count = 2;
    assert(ou::hit(v, 160, 60 + 78) == ou::Action::add);
}
} // namespace
int main(int argc, char **argv) {
    assert(argc == 2);
    models();
    config();
    ui(argv[1]);
    std::puts(
        "USAGE_PROVIDERS feed/OpenRouter models, configuration, sources and 34px targets PASS");
}
