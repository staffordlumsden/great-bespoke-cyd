#pragma once
#include "private_config.h"
#include "usage_model.h"
namespace ou {
enum class FetchError {
    none,
    memory,
    transport,
    too_large,
    content_type,
    incomplete,
    http,
    json,
    stale
};
struct FetchResult {
    Metrics metrics{};
    Balance balance{};
    Usage usage{};
    bool balance_ok = false, usage_ok = false;
    int status = 0;
    uint32_t retry_after = 0;
    FetchError error = FetchError::none;
    Error model_error = Error::none;
};
// One persistent worker; calls contain only a pointer to immutable RAM credentials.
// No secrets, payloads or account information are logged or persisted.
bool fetch_init();
bool fetch_start(const ProviderConfig *);
bool fetch_result(FetchResult &);
const char *fetch_error_name(FetchError);
} // namespace ou
