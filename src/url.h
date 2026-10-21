#pragma once

#include <string>

struct WebUrl {
    std::string url;
    std::string display;
};

// Accepts an absolute http(s) URL or a bare domain / URL without a scheme.
// Rejects credentials, whitespace, control characters and non-web schemes.
bool TryMakeWebUrl(const std::string& raw, WebUrl* out);
