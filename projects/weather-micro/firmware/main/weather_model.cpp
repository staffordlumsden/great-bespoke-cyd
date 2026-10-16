#include "weather_model.h"
#include "json_helpers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
namespace wm {
namespace {
constexpr int64_t first_epoch = 1704067200, last_epoch = 4102444799;
bool text(const cJSON *p, char *out, size_t cap, bool required = true) {
    if (!cJSON_IsString(p)) {
        out[0] = 0;
        return !required;
    }
    const auto *s = reinterpret_cast<const unsigned char *>(p->valuestring);
    size_t n = 0;
    for (size_t i = 0; s[i];) {
        unsigned c = s[i++];
        if (c < 32 || c == 127)
            return false;
        if (c >= 128) {
            if (c >= 0xc2 && c <= 0xdf && s[i] >= 0x80 && s[i] <= 0xbf) {
                c = ((c & 31) << 6) | (s[i++] & 63);
                if (c >= 0xc0 && c <= 0xc5)
                    c = 'A';
                else if (c >= 0xe0 && c <= 0xe5)
                    c = 'a';
                else if (c == 0xc7)
                    c = 'C';
                else if (c == 0xe7)
                    c = 'c';
                else if (c >= 0xc8 && c <= 0xcb)
                    c = 'E';
                else if (c >= 0xe8 && c <= 0xeb)
                    c = 'e';
                else if (c >= 0xcc && c <= 0xcf)
                    c = 'I';
                else if (c >= 0xec && c <= 0xef)
                    c = 'i';
                else if (c == 0xd1)
                    c = 'N';
                else if (c == 0xf1)
                    c = 'n';
                else if (c >= 0xd2 && c <= 0xd6)
                    c = 'O';
                else if (c >= 0xf2 && c <= 0xf6)
                    c = 'o';
                else if (c >= 0xd9 && c <= 0xdc)
                    c = 'U';
                else if (c >= 0xf9 && c <= 0xfc)
                    c = 'u';
                else
                    c = '?';
            } else {
                while (s[i] >= 0x80 && s[i] <= 0xbf)
                    ++i;
                c = '?';
            }
        }
        if (n + 1 < cap)
            out[n++] = char(c);
    }
    while (n && out[n - 1] == ' ')
        --n;
    out[n] = 0;
    return !required || n;
}
bool location(const cJSON *p, Location &l) {
    auto *id = item(p, "id"), *lat = item(p, "latitude"), *lon = item(p, "longitude");
    if (!integer(id, 1, 9007199254740991.) || !number(lat, -90, 90) || !number(lon, -180, 180))
        return false;
    l.id = int64_t(id->valuedouble);
    l.latitude = lat->valuedouble;
    l.longitude = lon->valuedouble;
    if (!text(item(p, "name"), l.name, sizeof(l.name)) ||
        !text(item(p, "admin1"), l.admin, sizeof(l.admin), false) ||
        !text(item(p, "timezone"), l.timezone, sizeof(l.timezone)))
        return false;
    auto *country = item(p, "country_code");
    if (!cJSON_IsString(country) || std::strlen(country->valuestring) != 2)
        return false;
    for (unsigned i = 0; i < 2; ++i)
        if (country->valuestring[i] < 'A' || country->valuestring[i] > 'Z')
            return false;
    std::strcpy(l.country, country->valuestring);
    return true;
}
bool value(const cJSON *p, double low, double high, float &out, bool &valid) {
    valid = false;
    if (!p || cJSON_IsNull(p))
        return true;
    if (!number(p, low, high))
        return false;
    out = float(p->valuedouble);
    valid = true;
    return true;
}
int code(const cJSON *p) { return integer(p, 0, 99) ? int(p->valuedouble) : -1; }
bool stamp(const cJSON *p, int64_t &out, bool optional = false) {
    if (optional && (!p || cJSON_IsNull(p) || number(p, 0, 0))) {
        out = 0;
        return true;
    }
    if (!integer(p, first_epoch, last_epoch))
        return false;
    out = int64_t(p->valuedouble);
    return true;
}
const cJSON *array(const cJSON *p, const char *key, unsigned n) {
    auto *a = item(p, key);
    return cJSON_IsArray(a) && unsigned(cJSON_GetArraySize(a)) == n ? a : nullptr;
}
const cJSON *at(const cJSON *p, unsigned i) { return cJSON_GetArrayItem(p, int(i)); }
} // namespace
bool parse_search(const char *data, size_t size, SearchResults &out) {
    Json root{document(data, size)};
    if (!root || cJSON_IsTrue(item(root.p, "error")))
        return false;
    SearchResults next;
    auto *a = item(root.p, "results");
    if (!a) {
        out = next;
        return true;
    }
    if (!cJSON_IsArray(a) || cJSON_GetArraySize(a) > int(max_results))
        return false;
    for (auto *p = a->child; p; p = p->next) {
        Location l;
        if (!location(p, l))
            return false;
        bool dup = false;
        for (unsigned i = 0; i < next.count; ++i)
            dup |= next.places[i].id == l.id;
        if (!dup)
            next.places[next.count++] = l;
    }
    out = next;
    return true;
}
bool parse_forecast(const char *data, size_t size, Forecast &out) {
    Json root{document(data, size)};
    if (!root || cJSON_IsTrue(item(root.p, "error")))
        return false;
    Forecast next;
    const cJSON *r = root.p, *current = item(r, "current"), *hours = item(r, "hourly"),
                *days = item(r, "daily");
    if (!cJSON_IsObject(current) || !cJSON_IsObject(hours) || !cJSON_IsObject(days))
        return false;
    auto *offset = item(r, "utc_offset_seconds");
    if (!integer(offset, -50400, 50400) ||
        !text(item(r, "timezone"), next.timezone, sizeof(next.timezone)))
        return false;
    next.offset = offset->valueint;
    if (!unit(item(r, "current_units"), "temperature_2m",
              "\xC2\xB0"
              "C") ||
        !unit(item(r, "current_units"), "wind_speed_10m", "km/h") ||
        !unit(item(r, "hourly_units"), "temperature_2m",
              "\xC2\xB0"
              "C") ||
        !unit(item(r, "hourly_units"), "rain", "mm") ||
        !unit(item(r, "hourly_units"), "showers", "mm") ||
        !unit(item(r, "daily_units"), "temperature_2m_max",
              "\xC2\xB0"
              "C") ||
        !unit(item(r, "daily_units"), "temperature_2m_min",
              "\xC2\xB0"
              "C"))
        return false;
    if (!stamp(item(current, "time"), next.current_time) ||
        !value(item(current, "temperature_2m"), -100, 70, next.temperature, next.temp_ok))
        return false;
    next.code = code(item(current, "weather_code"));
    next.day = !integer(item(current, "is_day"), 0, 1) || item(current, "is_day")->valueint != 0;
    bool speed = false, direction = false;
    if (!value(item(current, "wind_speed_10m"), 0, 500, next.wind_speed, speed) ||
        !value(item(current, "wind_direction_10m"), 0, 360, next.wind_direction, direction))
        return false;
    next.wind_ok = speed && (direction || next.wind_speed == 0);
    auto *ht = item(hours, "time");
    if (!cJSON_IsArray(ht) || (next.hour_count = unsigned(cJSON_GetArraySize(ht))) < 24 ||
        next.hour_count > max_hours)
        return false;
    auto *temp = array(hours, "temperature_2m", next.hour_count),
         *rain = array(hours, "rain", next.hour_count),
         *showers = array(hours, "showers", next.hour_count),
         *codes = array(hours, "weather_code", next.hour_count),
         *daylight = array(hours, "is_day", next.hour_count);
    if (!temp || !rain || !showers || !codes || !daylight)
        return false;
    for (unsigned i = 0; i < next.hour_count; ++i) {
        auto &h = next.hours[i];
        float shower = 0, large = 0;
        bool a = false, b = false;
        if (!stamp(at(ht, i), h.time) || (i && h.time - next.hours[i - 1].time != 3600) ||
            !value(at(temp, i), -100, 70, h.temperature, h.temp_ok) ||
            !value(at(rain, i), 0, 1000, large, a) || !value(at(showers, i), 0, 1000, shower, b))
            return false;
        h.rain_ok = a && b;
        h.rain = large + shower;
        h.code = code(at(codes, i));
        h.day = !integer(at(daylight, i), 0, 1) || at(daylight, i)->valueint != 0;
    }
    auto *dt = item(days, "time");
    if (!cJSON_IsArray(dt) || (next.day_count = unsigned(cJSON_GetArraySize(dt))) < 1 ||
        next.day_count > max_days)
        return false;
    auto *high = array(days, "temperature_2m_max", next.day_count),
         *low = array(days, "temperature_2m_min", next.day_count),
         *rise = array(days, "sunrise", next.day_count),
         *set = array(days, "sunset", next.day_count),
         *phase = array(days, "moon_phase", next.day_count);
    if (!high || !low || !rise || !set || !phase)
        return false;
    for (unsigned i = 0; i < next.day_count; ++i) {
        auto &d = next.days[i];
        if (!stamp(at(dt, i), d.time) || (i && d.time <= next.days[i - 1].time) ||
            !stamp(at(rise, i), d.rise, true) || !stamp(at(set, i), d.set, true) ||
            !value(at(high, i), -100, 70, d.high, d.high_ok) ||
            !value(at(low, i), -100, 70, d.low, d.low_ok) ||
            !value(at(phase, i), 0, 1, d.moon, d.moon_ok))
            return false;
        if (d.high_ok && d.low_ok && d.high < d.low)
            return false;
    }
    out = next;
    return true;
}
bool decode_settings(const char *data, size_t n, Settings &out) {
    Json root{document(data, n, max_settings)};
    if (!root || !integer(item(root.p, "version"), 1, 1))
        return false;
    Settings s;
    auto *places = item(root.p, "locations"), *selected = item(root.p, "selected"),
         *f = item(root.p, "fahrenheit"), *l = item(root.p, "light");
    if (!cJSON_IsArray(places) || cJSON_GetArraySize(places) > int(max_places) ||
        !integer(selected, 0, max_places - 1) || !cJSON_IsBool(f) || !cJSON_IsBool(l))
        return false;
    s.fahrenheit = cJSON_IsTrue(f);
    s.light = cJSON_IsTrue(l);
    s.selected = selected->valueint;
    for (auto *p = places->child; p; p = p->next) {
        if (!location(p, s.places[s.count]))
            return false;
        for (unsigned i = 0; i < s.count; ++i)
            if (s.places[i].id == s.places[s.count].id)
                return false;
        ++s.count;
    }
    if (s.selected >= (s.count ? s.count : 1))
        return false;
    out = s;
    return true;
}
bool encode_settings(const Settings &s, char *out, size_t cap) {
    if (s.count > max_places || s.selected >= (s.count ? s.count : 1) || cap < 2)
        return false;
    Json root{cJSON_CreateObject()};
    if (!root)
        return false;
    cJSON_AddNumberToObject(root.p, "version", 1);
    cJSON_AddBoolToObject(root.p, "fahrenheit", s.fahrenheit);
    cJSON_AddBoolToObject(root.p, "light", s.light);
    cJSON_AddNumberToObject(root.p, "selected", s.selected);
    auto *list = cJSON_AddArrayToObject(root.p, "locations");
    if (!list)
        return false;
    for (unsigned i = 0; i < s.count; ++i) {
        auto *p = cJSON_CreateObject();
        if (!p)
            return false;
        cJSON_AddItemToArray(list, p);
        const auto &l = s.places[i];
        cJSON_AddNumberToObject(p, "id", double(l.id));
        cJSON_AddNumberToObject(p, "latitude", l.latitude);
        cJSON_AddNumberToObject(p, "longitude", l.longitude);
        cJSON_AddStringToObject(p, "name", l.name);
        cJSON_AddStringToObject(p, "admin1", l.admin);
        cJSON_AddStringToObject(p, "country_code", l.country);
        cJSON_AddStringToObject(p, "timezone", l.timezone);
    }
    if (!cJSON_PrintPreallocated(root.p, out, int(cap), false))
        return false;
    Settings check;
    return decode_settings(out, std::strlen(out), check);
}
bool save_place(Settings &s, const Location &l) {
    for (unsigned i = 0; i < s.count; ++i)
        if (s.places[i].id == l.id) {
            s.places[i] = l;
            s.selected = i;
            return true;
        }
    if (s.count == max_places)
        return false;
    s.selected = s.count;
    s.places[s.count++] = l;
    return true;
}
void remove_place(Settings &s, unsigned index) {
    if (index >= s.count)
        return;
    for (unsigned i = index + 1; i < s.count; ++i)
        s.places[i - 1] = s.places[i];
    s.places[--s.count] = {};
    if (s.selected > index)
        --s.selected;
    if (s.selected >= s.count)
        s.selected = s.count ? s.count - 1 : 0;
}
const char *condition(int code) {
    switch (code) {
    case 0:
        return "Clear sky";
    case 1:
        return "Mainly clear";
    case 2:
        return "Partly cloudy";
    case 3:
        return "Overcast";
    case 45:
        return "Fog";
    case 48:
        return "Freezing fog";
    case 51:
        return "Light drizzle";
    case 53:
        return "Moderate drizzle";
    case 55:
        return "Heavy drizzle";
    case 56:
    case 57:
        return "Freezing drizzle";
    case 61:
        return "Light rain";
    case 63:
        return "Moderate rain";
    case 65:
        return "Heavy rain";
    case 66:
    case 67:
        return "Freezing rain";
    case 71:
        return "Light snow";
    case 73:
        return "Moderate snow";
    case 75:
        return "Heavy snow";
    case 77:
        return "Snow grains";
    case 80:
        return "Light showers";
    case 81:
        return "Rain showers";
    case 82:
        return "Heavy showers";
    case 85:
        return "Snow showers";
    case 86:
        return "Heavy snow showers";
    case 95:
    case 97:
        return "Thunderstorm";
    case 96:
    case 99:
        return "Storm with hail";
    default:
        return "Not available";
    }
}
float temperature(float c, bool f) { return f ? c * 1.8f + 32.f : c; }
void hour_label(int64_t epoch, int offset, char *out, size_t cap, bool minutes) {
    if (epoch < first_epoch || epoch > last_epoch) {
        std::snprintf(out, cap, "--");
        return;
    }
    time_t value = epoch + offset;
    tm t{};
    gmtime_r(&value, &t);
    if (minutes)
        std::snprintf(out, cap, "%d:%02d%s", t.tm_hour % 12 ? t.tm_hour % 12 : 12, t.tm_min,
                      t.tm_hour < 12 ? "am" : "pm");
    else if (t.tm_hour == 12)
        std::snprintf(out, cap, "Noon");
    else
        std::snprintf(out, cap, "%d%s", t.tm_hour % 12 ? t.tm_hour % 12 : 12,
                      t.tm_hour < 12 ? "am" : "pm");
}
unsigned next_hour(const Forecast &f, int64_t epoch) {
    unsigned i = 0;
    while (i < f.hour_count && f.hours[i].time <= epoch)
        ++i;
    return i;
}
int today(const Forecast &f, int64_t epoch) {
    time_t value = epoch + f.offset;
    tm now{};
    gmtime_r(&value, &now);
    for (unsigned i = 0; i < f.day_count; ++i) {
        value = f.days[i].time + f.offset;
        tm day{};
        gmtime_r(&value, &day);
        if (day.tm_year == now.tm_year && day.tm_yday == now.tm_yday)
            return int(i);
    }
    return -1;
}
void forecast_url(const Location &l, char *out, size_t cap) {
    int n = std::snprintf(
        out, cap,
        "https://api.open-meteo.com/v1/"
        "forecast?latitude=%.5f&longitude=%.5f&current=temperature_2m,weather_code,is_day,wind_"
        "speed_10m,wind_direction_10m&hourly=temperature_2m,weather_code,is_day,rain,showers&daily="
        "temperature_2m_max,temperature_2m_min,sunrise,sunset,moon_phase&timezone=auto&timeformat="
        "unixtime&temperature_unit=celsius&wind_speed_unit=kmh&precipitation_unit=mm&forecast_days="
        "3&forecast_hours=30",
        l.latitude, l.longitude);
    if (n < 0 || size_t(n) >= cap)
        out[0] = 0;
}
bool search_url(const char *term, char *out, size_t cap) {
    if (!term)
        return false;
    size_t len = std::strlen(term);
    if (len < 2 || len > 40)
        return false;
    char encoded[121]{};
    size_t n = 0;
    const char *hex = "0123456789ABCDEF";
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = term[i];
        if (c < 32 || c > 126)
            return false;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '.')
            encoded[n++] = c;
        else {
            encoded[n++] = '%';
            encoded[n++] = hex[c >> 4];
            encoded[n++] = hex[c & 15];
        }
    }
    int size = std::snprintf(
        out, cap,
        "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=8&language=en&format=json",
        encoded);
    return size > 0 && size_t(size) < cap;
}
const char *moon_name(float p) {
    unsigned n = unsigned(std::floor((p - std::floor(p)) * 8.f + .5f)) % 8;
    const char *names[] = {"New moon",  "Waxing crescent", "First quarter", "Waxing gibbous",
                           "Full moon", "Waning gibbous",  "Last quarter",  "Waning crescent"};
    return names[n];
}
const char *wind_name(float d) {
    const char *names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return names[unsigned(std::floor((d + 22.5f) / 45.f)) % 8];
}
bool moon_lit(float x, float y, float phase, bool south) {
    float r = x * x + y * y;
    if (r > 1)
        return false;
    float angle = phase * 6.28318530718f;
    return (south ? -x : x) * std::sin(angle) - std::sqrt(std::max(0.f, 1 - r)) * std::cos(angle) >
           0;
}
float sun_progress(const Day &d, int64_t epoch) {
    if (!d.rise || d.set <= d.rise || epoch < d.rise || epoch > d.set)
        return -1;
    return float(epoch - d.rise) / float(d.set - d.rise);
}
} // namespace wm
