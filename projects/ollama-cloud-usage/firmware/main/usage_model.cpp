#include "usage_model.h"
#include "cJSON.h"
#include "json_guard.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace ou {
namespace {
const cJSON *item(const cJSON *p, const char *k) { return cJSON_GetObjectItemCaseSensitive(p, k); }
bool unique(const cJSON *p, unsigned depth = 0) {
    if (depth > 12)
        return false;
    for (auto *a = p->child; a; a = a->next) {
        if (cJSON_IsObject(p))
            for (auto *b = a->next; b; b = b->next)
                if (a->string && b->string && !std::strcmp(a->string, b->string))
                    return false;
        if (!unique(a, depth + 1))
            return false;
    }
    return true;
}
struct Json {
    cJSON *root = nullptr;
    ~Json() { cJSON_Delete(root); }
    bool read(const char *s, size_t n, Error &e) {
        e = Error::size;
        if (!s || !n || n > response_limit)
            return false;
        if (!json_guard(s, n, 12)) {
            e = Error::json;
            return false;
        }
        const char *end = nullptr;
        root = cJSON_ParseWithLengthOpts(s, n, &end, 0);
        e = Error::json;
        if (!root || !end || end < s || end > s + n)
            return false;
        while (end < s + n && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t'))
            ++end;
        if (end != s + n || !cJSON_IsObject(root) || !unique(root))
            return false;
        e = Error::schema;
        return true;
    }
};
bool number(const cJSON *p, double &v) {
    if (!cJSON_IsNumber(p) || !std::isfinite(p->valuedouble))
        return false;
    v = p->valuedouble;
    return std::abs(v) <= 1e12;
}
bool unsigned_number(const cJSON *p, uint64_t &v) {
    if (!cJSON_IsNumber(p))
        return false;
    double x = p->valuedouble;
    if (!std::isfinite(x) || x < 0 || x > 9007199254740991.0 || std::floor(x) != x)
        return false;
    v = static_cast<uint64_t>(x);
    return true;
}
bool optional_count(const cJSON *p, const char *k, uint64_t &n, bool &known) {
    auto *v = item(p, k);
    known = v != nullptr;
    return !known || unsigned_number(v, n);
}
bool time_field(const cJSON *p, const char *key, int64_t &v) {
    auto *s = item(p, key);
    return cJSON_IsString(s) && parse_timestamp(s->valuestring, v);
}
bool eq(const cJSON *p, const char *key, const char *expected) {
    auto *s = item(p, key);
    return cJSON_IsString(s) && !std::strcmp(s->valuestring, expected);
}
bool digits(const char *s, unsigned n, int &v) {
    v = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9')
            return false;
        v = v * 10 + s[i] - '0';
    }
    return true;
}
bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
} // namespace
bool parse_timestamp(const char *s, int64_t &result) {
    if (!s)
        return false;
    const size_t n = std::strlen(s);
    if (n < 20 || n > 40)
        return false;
    int y, m, d, h, mi, se;
    if (!digits(s, 4, y) || s[4] != '-' || !digits(s + 5, 2, m) || s[7] != '-' ||
        !digits(s + 8, 2, d) || (s[10] != 'T' && s[10] != 't') || !digits(s + 11, 2, h) ||
        s[13] != ':' || !digits(s + 14, 2, mi) || s[16] != ':' || !digits(s + 17, 2, se))
        return false;
    if (y < 1970 || y > 2200 || m < 1 || m > 12 || h > 23 || mi > 59 || se > 59)
        return false;
    const int md[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (d < 1 || d > md[m - 1] + (m == 2 && leap(y)))
        return false;
    size_t i = 19;
    if (s[i] == '.') {
        ++i;
        size_t first = i;
        while (s[i] >= '0' && s[i] <= '9')
            ++i;
        if (i == first)
            return false;
    }
    int offset = 0;
    if (s[i] == 'Z' || s[i] == 'z') {
        if (i + 1 != n)
            return false;
    } else if (s[i] == '+' || s[i] == '-') {
        int oh, om;
        bool positive = s[i] == '+';
        if (i + 6 != n || !digits(s + i + 1, 2, oh) || s[i + 3] != ':' ||
            !digits(s + i + 4, 2, om) || oh > 23 || om > 59)
            return false;
        offset = (oh * 60 + om) * 60 * (positive ? 1 : -1);
    } else
        return false;
    int64_t days = 0;
    for (int year = 1970; year < y; ++year)
        days += leap(year) ? 366 : 365;
    for (int month = 1; month < m; ++month)
        days += md[month - 1] + (month == 2 && leap(y));
    result = (days + d - 1) * 86400 + h * 3600 + mi * 60 + se - offset;
    return true;
}
bool parse_balance(const char *s, size_t n, Balance &out, Error &error) {
    Json j;
    if (!j.read(s, n, error))
        return false;
    Balance b{};
    auto *inc = item(j.root, "included");
    auto *purchased = item(j.root, "purchased");
    if (!cJSON_IsObject(inc) || !cJSON_IsObject(purchased) ||
        !number(item(purchased, "balance_usd"), b.purchased))
        return false;
    if (item(inc, "allowance_usd")) {
        b.kind = BalanceKind::credits;
        if (!number(item(inc, "balance_usd"), b.remaining) ||
            !number(item(inc, "allowance_usd"), b.allowance) || b.allowance < 0)
            return false;
        auto *period = item(inc, "period");
        if (!cJSON_IsObject(period) || !time_field(period, "from", b.period_from) ||
            !time_field(period, "until", b.period_until) || b.period_until <= b.period_from)
            return false;
        b.percent_known = b.allowance > 0;
        if (b.percent_known)
            b.used_percent = std::max(0.0, (b.allowance - b.remaining) * 100 / b.allowance);
        if (!std::isfinite(b.used_percent))
            return false;
    } else {
        b.kind = BalanceKind::legacy;
        auto *session = item(inc, "session");
        auto *weekly = item(inc, "weekly");
        double sr = 0, wr = 0;
        if (!cJSON_IsObject(session) || !cJSON_IsObject(weekly) ||
            !number(item(session, "remaining_percent"), sr) ||
            !number(item(weekly, "remaining_percent"), wr) || sr < 0 || sr > 100 || wr < 0 ||
            wr > 100 || !time_field(session, "resets_at", b.session_reset) ||
            !time_field(weekly, "resets_at", b.weekly_reset))
            return false;
        b.session_used = 100 - sr;
        b.weekly_used = 100 - wr;
    }
    out = b;
    error = Error::none;
    return true;
}
bool parse_usage(const char *s, size_t n, Usage &out, Error &error) {
    Json j;
    if (!j.read(s, n, error))
        return false;
    Usage u{};
    if (!eq(j.root, "range", "24h") || !eq(j.root, "scope", "self") ||
        !eq(j.root, "granularity", "hour") || !time_field(j.root, "from", u.from) ||
        !time_field(j.root, "until", u.until) || u.until <= u.from)
        return false;
    auto *total = item(j.root, "totals");
    auto *buckets = item(j.root, "buckets");
    if (!cJSON_IsObject(total) || !unsigned_number(item(total, "request_count"), u.requests) ||
        !cJSON_IsArray(buckets) || cJSON_GetArraySize(buckets) > 25 ||
        !optional_count(total, "input_tokens", u.input, u.input_known) ||
        !optional_count(total, "output_tokens", u.output, u.output_known) ||
        !optional_count(total, "cached_input_tokens", u.cached, u.cached_known))
        return false;
    auto *cost = item(total, "usage_usd");
    u.cost_known = cost != nullptr;
    if (u.cost_known && (!number(cost, u.cost) || u.cost < 0))
        return false;
    if (u.cached_known && u.input_known && u.cached > u.input)
        return false;
    int64_t previous = 0;
    uint64_t sum = 0;
    for (auto *b = buckets->child; b; b = b->next) {
        int64_t from, until;
        uint64_t requests;
        if (!cJSON_IsObject(b) || !time_field(b, "from", from) || !time_field(b, "until", until) ||
            until <= from || (u.bucket_count && from < previous) ||
            !unsigned_number(item(b, "request_count"), requests))
            return false;
        if (sum > std::numeric_limits<uint64_t>::max() - requests)
            return false;
        sum += requests;
        u.buckets[u.bucket_count++] = requests;
        previous = until;
    }
    // Totals can be updated independently while the current partial bucket is live.
    out = u;
    error = Error::none;
    return true;
}
void countdown(int64_t reset, int64_t now, char *out, size_t capacity) {
    if (!reset || now <= 0) {
        std::snprintf(out, capacity, "Reset unavailable");
        return;
    }
    if (reset <= now) {
        std::snprintf(out, capacity, "Reset due");
        return;
    }
    auto minutes = (reset - now + 59) / 60;
    if (minutes >= 1440)
        std::snprintf(out, capacity, "Reset in %lldd %lldh", static_cast<long long>(minutes / 1440),
                      static_cast<long long>(minutes % 1440 / 60));
    else if (minutes >= 60)
        std::snprintf(out, capacity, "Reset in %lldh %lldm", static_cast<long long>(minutes / 60),
                      static_cast<long long>(minutes % 60));
    else
        std::snprintf(out, capacity, "Reset in %lldm", static_cast<long long>(minutes));
}
void amount(double value, char *out, size_t capacity) {
    if (std::abs(value) >= 1e6)
        std::snprintf(out, capacity, "$%.1fM", value / 1e6);
    else if (std::abs(value) >= 1e4)
        std::snprintf(out, capacity, "$%.1fk", value / 1e3);
    else
        std::snprintf(out, capacity, "$%.2f", value);
}
void count(uint64_t value, char *out, size_t capacity) {
    if (value >= 1000000000ULL)
        std::snprintf(out, capacity, "%.1fG", static_cast<double>(value) / 1e9);
    else if (value >= 1000000ULL)
        std::snprintf(out, capacity, "%.1fM", static_cast<double>(value) / 1e6);
    else if (value >= 10000)
        std::snprintf(out, capacity, "%.1fk", static_cast<double>(value) / 1e3);
    else
        std::snprintf(out, capacity, "%llu", static_cast<unsigned long long>(value));
}
void PollPolicy::finish(uint64_t now, bool success, int status, uint32_t retry_after) {
    if (success)
        failures = 0;
    else
        failures = std::min(5U, failures + 1);
    uint64_t delay = success ? 60000 : std::min<uint64_t>(600000, 60000ULL << (failures - 1));
    if (retry_after)
        delay = std::max(delay, uint64_t(retry_after) * 1000);
    due = now + delay;
    blocked = status == 401 || status == 403;
}
bool PollPolicy::manual(uint64_t now) {
    blocked = false;
    const uint64_t earliest = started ? last_start + 60000 : 0;
    due = std::max(due, earliest);
    return now >= due;
}
uint32_t retry_seconds(const char *value) {
    if (!value || !*value)
        return 0;
    uint64_t n = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
        if (*p < '0' || *p > '9')
            return 0;
        n = n * 10 + *p - '0';
        if (n >= UINT32_MAX)
            return UINT32_MAX;
    }
    return static_cast<uint32_t>(n);
}
const char *error_name(Error e) {
    switch (e) {
    case Error::none:
        return "OK";
    case Error::size:
        return "RESPONSE TOO LARGE";
    case Error::json:
        return "INVALID JSON";
    case Error::schema:
        return "UNSUPPORTED DATA";
    case Error::range:
        return "INVALID VALUE";
    }
    return "ERROR";
}
} // namespace ou
