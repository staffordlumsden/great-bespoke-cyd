#pragma once
#include "weather_model.h"
namespace wm {
enum class JobKind { forecast, search };
enum class FetchError { none, memory, transport, http, content_type, too_large, incomplete, json };
struct Job {
    JobKind kind = JobKind::forecast;
    Location location{};
    char query[41]{};
    uint32_t generation = 0;
};
struct Result {
    JobKind kind = JobKind::forecast;
    int64_t place_id = 0;
    uint32_t generation = 0;
    bool ok = false;
    FetchError error = FetchError::none;
    int status = 0;
    unsigned retry_after = 0;
    Forecast forecast{};
    SearchResults search{};
};
bool fetch_init();
bool fetch_start(const Job &);
bool fetch_result(Result &);
const char *fetch_error(FetchError);
} // namespace wm
