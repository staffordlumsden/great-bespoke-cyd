#pragma once
#include <cstddef>
#include <cstdint>
namespace wm {
constexpr unsigned max_places = 6, max_results = 8, max_hours = 32, max_days = 4;
constexpr size_t max_response = 24576, max_settings = 4096;
struct Location {
    int64_t id = 0;
    double latitude = 0, longitude = 0;
    char name[41]{}, admin[33]{}, country[3]{}, timezone[49]{};
};
struct Settings {
    bool fahrenheit = false, light = false;
    unsigned count = 0, selected = 0;
    Location places[max_places]{};
};
struct Hour {
    int64_t time = 0;
    float temperature = 0, rain = 0;
    int code = -1;
    bool temp_ok = false, rain_ok = false, day = true;
};
struct Day {
    int64_t time = 0, rise = 0, set = 0;
    float high = 0, low = 0, moon = 0;
    bool high_ok = false, low_ok = false, moon_ok = false;
};
struct Forecast {
    int64_t current_time = 0;
    int offset = 0, code = -1;
    float temperature = 0, wind_speed = 0, wind_direction = 0;
    bool temp_ok = false, wind_ok = false, day = true;
    unsigned hour_count = 0, day_count = 0;
    Hour hours[max_hours]{};
    Day days[max_days]{};
    char timezone[49]{};
};
struct SearchResults {
    unsigned count = 0;
    Location places[max_results]{};
};
bool parse_forecast(const char *, size_t, Forecast &);
bool parse_search(const char *, size_t, SearchResults &);
bool decode_settings(const char *, size_t, Settings &);
bool encode_settings(const Settings &, char *, size_t);
bool save_place(Settings &, const Location &);
void remove_place(Settings &, unsigned);
const char *condition(int code);
float temperature(float celsius, bool fahrenheit);
void hour_label(int64_t epoch, int offset, char *, size_t, bool minutes = false);
unsigned next_hour(const Forecast &, int64_t epoch);
int today(const Forecast &, int64_t epoch);
void forecast_url(const Location &, char *, size_t);
bool search_url(const char *, char *, size_t);
const char *moon_name(float phase);
const char *wind_name(float degrees);
bool moon_lit(float x, float y, float phase, bool south);
float sun_progress(const Day &, int64_t epoch);
} // namespace wm
