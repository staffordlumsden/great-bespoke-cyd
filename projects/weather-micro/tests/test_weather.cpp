#include "cJSON.h"
#include "cache_model.h"
#include "ui.h"
#include "weather_model.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
using namespace wm;
std::string read(const std::string &name) {
    std::ifstream in(name);
    std::ostringstream out;
    out << in.rdbuf();
    assert(in.good() || in.eof());
    return out.str();
}
void raster(const View &v, const std::string &name) {
    prepare(v);
    assert(scene_valid());
    std::ofstream out(name, std::ios::binary);
    out << "P6\n320 240\n255\n";
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x) {
            auto p = pixel(x, y);
            unsigned char rgb[] = {static_cast<unsigned char>(((p >> 11) & 31) * 255 / 31),
                                   static_cast<unsigned char>(((p >> 5) & 63) * 255 / 63),
                                   static_cast<unsigned char>((p & 31) * 255 / 31)};
            out.write(reinterpret_cast<const char *>(rgb), 3);
        }
    assert(out.good());
}
int main(int argc, char **argv) {
    assert(argc == 3);
    std::string base = argv[1], output = argv[2];
    auto forecast = read(base + "/forecast.json"), search = read(base + "/search.json");
    Forecast f;
    SearchResults r;
    assert(parse_forecast(forecast.data(), forecast.size(), f));
    assert(f.hour_count == 30 && f.day_count >= 1 && f.temp_ok);
    assert(parse_search(search.data(), search.size(), r) && r.count > 1 && r.count <= 8);
    assert(today(f, f.current_time) >= 0);
    assert(next_hour(f, f.current_time) + 24 <= f.hour_count);
    assert(std::fabs(temperature(0, true) - 32) < .01 &&
           std::fabs(temperature(100, true) - 212) < .01);
    assert(temperature(-40, true) == -40);
    char label[32];
    hour_label(1704110400, 0, label, sizeof(label));
    assert(!std::strcmp(label, "Noon"));
    hour_label(1704067200, 0, label, sizeof(label));
    assert(!std::strcmp(label, "12am"));
    hour_label(1704067200, 19800, label, sizeof(label), true);
    assert(!std::strcmp(label, "5:30am"));
    for (int code = -1; code <= 110; ++code) {
        const char *name = condition(code);
        unsigned words = 1;
        for (auto *p = name; *p; ++p)
            words += *p == ' ';
        assert(words <= 3);
    }
    assert(!moon_lit(0, 0, 0, false) && moon_lit(0, 0, .5, false));
    assert(moon_lit(.5, 0, .25, false) && !moon_lit(-.5, 0, .25, false));
    assert(moon_lit(-.5, 0, .25, true));
    assert(!moon_lit(.5, 0, .75, false) && moon_lit(-.5, 0, .75, false));
    assert(!std::strcmp(wind_name(0), "N") && !std::strcmp(wind_name(90), "E") &&
           !std::strcmp(wind_name(360), "N"));
    Day day;
    day.rise = 1704067200;
    day.set = day.rise + 43200;
    assert(sun_progress(day, day.rise) == 0 && sun_progress(day, day.set) == 1 &&
           sun_progress(day, day.rise - 1) < 0);
    Settings s;
    assert(save_place(s, r.places[0]) && s.count == 1);
    assert(save_place(s, r.places[0]) && s.count == 1);
    for (unsigned i = 1; i < r.count && s.count < max_places; ++i)
        assert(save_place(s, r.places[i]));
    s.fahrenheit = true;
    s.light = true;
    char settings[max_settings];
    assert(encode_settings(s, settings, sizeof(settings)));
    Settings decoded;
    assert(decode_settings(settings, std::strlen(settings), decoded) && decoded.count == s.count &&
           decoded.fahrenheit && decoded.light && decoded.places[0].id == s.places[0].id);
    assert(!decode_settings("{}", 2, decoded));
    remove_place(s, 0);
    assert(s.count > 0 && s.selected < s.count);
    CacheBook book;
    Settings locations;
    save_place(locations, r.places[0]);
    save_place(locations, r.places[1]);
    auto *first = book.response(locations, r.places[0].id);
    assert(first);
    first->forecast = f;
    first->has = true;
    auto *second = book.find(r.places[1].id);
    assert(second && !second->has && second != first);
    locations.selected = 1;
    assert(book.find(locations.places[locations.selected].id) == second);
    assert(book.response(locations, r.places[0].id) == first); // delayed reply retains its owner
    remove_place(locations, 0);
    book.prune(locations);
    assert(!book.response(locations, r.places[0].id) && book.find(r.places[1].id) == second);
    char url[768];
    assert(search_url("Sydney, AU", url, sizeof(url)) && std::strstr(url, "Sydney%2C%20AU"));
    assert(!search_url("A", url, sizeof(url)));
    assert(!search_url("abc\n", url, sizeof(url)));
    assert(!search_url("Sydney", url, 8));
    forecast_url(r.places[0], url, sizeof(url));
    assert(std::strstr(url, "https://api.open-meteo.com/") &&
           std::strstr(url, "forecast_hours=30"));
    Forecast unchanged = f;
    assert(!parse_forecast("{}", 2, f) && f.current_time == unchanged.current_time);
    assert(!parse_forecast(forecast.data(), forecast.size() - 8, f));
    std::string nul = forecast;
    nul.insert(4, 1, '\0');
    assert(!parse_forecast(nul.data(), nul.size(), f));
    std::string dup = forecast;
    dup.insert(1, "\"utc_offset_seconds\":0,");
    assert(!parse_forecast(dup.data(), dup.size(), f));
    assert(!parse_search("{\"results\":null}", 16, r));
    auto *json = cJSON_Parse(forecast.c_str());
    assert(json);
    auto *hours = cJSON_GetObjectItem(json, "hourly");
    cJSON_DeleteItemFromArray(cJSON_GetObjectItem(hours, "temperature_2m"), 0);
    char *bad = cJSON_PrintUnformatted(json);
    assert(!parse_forecast(bad, std::strlen(bad), f));
    cJSON_free(bad);
    cJSON_Delete(json);
    json=cJSON_Parse(forecast.c_str());assert(json);
    auto *daily=cJSON_GetObjectItem(json,"daily");
    cJSON_ReplaceItemInArray(cJSON_GetObjectItem(daily,"sunrise"),0,cJSON_CreateNumber(0));
    cJSON_ReplaceItemInArray(cJSON_GetObjectItem(daily,"sunset"),0,cJSON_CreateNull());
    cJSON_ReplaceItemInArray(cJSON_GetObjectItem(daily,"moon_phase"),0,cJSON_CreateNull());
    cJSON_ReplaceItemInArray(cJSON_GetObjectItem(cJSON_GetObjectItem(json,"hourly"),"temperature_2m"),0,cJSON_CreateNull());
    bad=cJSON_PrintUnformatted(json);assert(parse_forecast(bad,std::strlen(bad),f));
    assert(!f.days[0].rise&&!f.days[0].set&&!f.days[0].moon_ok&&!f.hours[0].temp_ok);
    cJSON_free(bad);cJSON_Delete(json);
    json=cJSON_Parse(forecast.c_str());assert(json);
    cJSON_ReplaceItemInObject(cJSON_GetObjectItem(json,"current_units"),"temperature_2m",cJSON_CreateString("F"));
    bad=cJSON_PrintUnformatted(json);assert(!parse_forecast(bad,std::strlen(bad),f));cJSON_free(bad);cJSON_Delete(json);
    std::string oversized(max_response+1,' ');assert(!parse_forecast(oversized.data(),oversized.size(),f));
    assert(parse_forecast(forecast.data(), forecast.size(), f));
    assert(parse_search(search.data(), search.size(), r));
    View v;
    v.forecast = f;
    v.has = true;
    v.now = f.current_time;
    v.connected = true;
    v.clock_valid = true;
    save_place(v.settings, r.places[0]);
    save_place(v.settings, r.places[1]);
    v.settings.selected = 0;
    v.results = r;
    std::strcpy(v.query, "SYDNEY, AU");
    const char *pages[] = {"current", "hourly", "details", "settings", "locations",
                           "place",   "search", "results", "network",  "about"};
    for (bool light : {false, true})
        for (unsigned i = 0; i < 10; ++i) {
            v.settings.light = light;
            v.page = static_cast<Page>(i);
            prepare(v);
            if (!scene_valid())
                std::fprintf(stderr, "Invalid scene %s\n", pages[i]);
            assert(scene_valid());
            Button b[40];
            unsigned n = buttons(v, b, 40);
            for (unsigned k = 0; k < n; ++k) {
                auto rect = b[k].rect;
                assert(rect.w >= 34 && rect.h >= 34 && rect.x >= 0 && rect.y >= 0 &&
                       rect.x + rect.w <= 320 && rect.y + rect.h <= 240);
                assert(hit(v, rect.x + rect.w / 2, rect.y + rect.h / 2) ==
                       (b[k].enabled ? b[k].action : Action{}));
                for (unsigned j = k + 1; j < n; ++j) {
                    auto q = b[j].rect;
                    assert(rect.x + rect.w <= q.x || q.x + q.w <= rect.x ||
                           rect.y + rect.h <= q.y || q.y + q.h <= rect.y);
                }
            }
            raster(v, output + "/" + (light ? "light-" : "dark-") + pages[i] + ".ppm");
        }
    assert(buttons(v,nullptr,0)==0);
    Button bounded[2];assert(buttons(v,bounded,2)==2);
    v.page = Page::search;
    v.keyboard_page = 1;
    prepare(v);
    assert(scene_valid());
    raster(v, output + "/search-more.ppm");
    v.page = Page::current;
    v.has = false;
    v.settings = {};
    raster(v, output + "/empty.ppm");
    v.page = Page::details;
    v.forecast = {};
    v.has = true;
    raster(v, output + "/missing.ppm");
    std::puts("PASS Open-Meteo live fixtures, strict parsing, local hours/Noon, C/F, settings, "
              "weather labels, sun/moon/wind and all touch/UI scenes");
}
