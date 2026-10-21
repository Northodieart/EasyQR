#include "url.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace {

std::string TrimAsciiSpace(const std::string& input) {
    size_t begin = 0;
    size_t end = input.size();
    while (begin < end) {
        const unsigned char c = static_cast<unsigned char>(input[begin]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++begin;
        } else {
            break;
        }
    }
    while (end > begin) {
        const unsigned char c = static_cast<unsigned char>(input[end - 1]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            --end;
        } else {
            break;
        }
    }
    return input.substr(begin, end - begin);
}

bool IsAlphaChar(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z');
}

bool IsDigitChar(char c) {
    return c >= '0' && c <= '9';
}

bool IsAlphaNum(char c) {
    return IsAlphaChar(c) || IsDigitChar(c);
}

char ToLowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

std::string ToLowerAscii(std::string s) {
    for (char& c : s) c = ToLowerAscii(c);
    return s;
}

std::string ToLowerHost(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = ToLowerAscii(c);
    return out;
}

bool IsValidPort(const std::string& port) {
    if (port.empty() || port.size() > 5) return false;
    int value = 0;
    for (char c : port) {
        if (!IsDigitChar(c)) return false;
        value = value * 10 + (c - '0');
    }
    return value >= 1 && value <= 65535;
}

struct Authority {
    std::string host;
    std::string port;
    bool ipv6 = false;
};

bool ParseAuthority(const std::string& authority, bool requireDot, Authority* out) {
    if (authority.empty()) return false;
    if (authority.find('@') != std::string::npos) return false;

    std::string hostPort = authority;
    std::string port;

    if (!hostPort.empty() && hostPort[0] == '[') {
        const size_t close = hostPort.find(']');
        if (close == std::string::npos) return false;
        out->ipv6 = true;
        out->host = hostPort.substr(1, close - 1);
        const std::string tail = hostPort.substr(close + 1);
        if (!tail.empty()) {
            if (tail[0] != ':') return false;
            port = tail.substr(1);
            if (!IsValidPort(port)) return false;
        }
        if (out->host.empty()) return false;
        for (char c : out->host) {
            const bool ok = IsDigitChar(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
                            c == ':' || c == '.';
            if (!ok) return false;
        }
        if (requireDot && out->host.find(':') == std::string::npos) return false;
        out->host = ToLowerHost(out->host);
        out->port = port;
        return true;
    }

    const size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        port = hostPort.substr(colon + 1);
        hostPort = hostPort.substr(0, colon);
        if (!IsValidPort(port)) return false;
    }
    if (hostPort.empty()) return false;
    if (hostPort.find(':') != std::string::npos) return false;

    // Non-ASCII host names are intentionally rejected: punycode is expected.
    for (char c : hostPort) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 0x80) return false;
    }

    bool allNumericDots = true;
    for (char c : hostPort) {
        if (!(IsDigitChar(c) || c == '.')) {
            allNumericDots = false;
            break;
        }
    }

    if (!allNumericDots) {
        if (hostPort.front() == '.' || hostPort.back() == '.') return false;
        if (hostPort.size() > 253) return false;
        size_t start = 0;
        while (start <= hostPort.size()) {
            size_t dot = hostPort.find('.', start);
            if (dot == std::string::npos) dot = hostPort.size();
            const std::string label = hostPort.substr(start, dot - start);
            if (label.empty() || label.size() > 63) return false;
            if (!IsAlphaNum(label.front()) || !IsAlphaNum(label.back())) return false;
            for (char c : label) {
                if (!(IsAlphaNum(c) || c == '-')) return false;
            }
            if (dot == hostPort.size()) break;
            start = dot + 1;
        }
        if (requireDot) {
            if (hostPort.find('.') == std::string::npos) return false;
            const size_t lastDot = hostPort.rfind('.');
            const std::string tld = hostPort.substr(lastDot + 1);
            if (tld.size() < 2 || tld.find_first_not_of("0123456789") == std::string::npos) {
                return false;
            }
        }
    } else {
        // IPv4-ish literal; require four numeric parts.
        int parts = 0;
        size_t start = 0;
        while (start <= hostPort.size()) {
            size_t dot = hostPort.find('.', start);
            if (dot == std::string::npos) dot = hostPort.size();
            const std::string part = hostPort.substr(start, dot - start);
            if (part.empty() || part.size() > 3) return false;
            int value = 0;
            for (char c : part) {
                if (!IsDigitChar(c)) return false;
                value = value * 10 + (c - '0');
            }
            if (value > 255) return false;
            ++parts;
            if (dot == hostPort.size()) break;
            start = dot + 1;
        }
        if (parts != 4) return false;
    }

    out->host = ToLowerHost(hostPort);
    out->port = port;
    return true;
}

std::string MakeDisplay(const Authority& authority, const std::string& scheme) {
    std::string host = authority.host;
    if (!authority.ipv6 && host.compare(0, 4, "www.") == 0 && host.size() > 4) {
        host = host.substr(4);
    }
    if (authority.ipv6) host = "[" + host + "]";
    const bool defaultPort = (scheme == "http" && authority.port == "80") ||
                             (scheme == "https" && authority.port == "443");
    if (!authority.port.empty() && !defaultPort) host += ":" + authority.port;
    return host;
}

bool HasControlOrSpace(const std::string& s) {
    for (char raw : s) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c <= 0x20 || c == 0x7f) return true;
        if (c == '<' || c == '>' || c == '"' || c == '`' || c == '\\') return true;
    }
    return false;
}

}  // namespace

bool TryMakeWebUrl(const std::string& raw, WebUrl* out) {
    if (!out) return false;
    out->url.clear();
    out->display.clear();

    const std::string input = TrimAsciiSpace(raw);
    if (input.empty() || input.size() > 4096) return false;
    if (HasControlOrSpace(input)) return false;

    std::string scheme;
    std::string rest;

    const size_t schemeMark = input.find("://");
    if (schemeMark != std::string::npos) {
        const std::string candidateScheme = input.substr(0, schemeMark);
        if (candidateScheme.empty() || !IsAlphaChar(candidateScheme.front())) return false;
        for (char c : candidateScheme) {
            if (!(IsAlphaNum(c) || c == '+' || c == '-' || c == '.')) return false;
        }
        scheme = ToLowerAscii(candidateScheme);
        if (scheme != "http" && scheme != "https") return false;
        rest = input.substr(schemeMark + 3);
        if (rest.empty()) return false;
    } else {
        // Bare input must look like a host, optionally followed by a path.
        const size_t hostEnd = input.find_first_of("/?#");
        std::string hostPart = hostEnd == std::string::npos ? input : input.substr(0, hostEnd);
        Authority bareAuthority;
        if (!ParseAuthority(hostPart, true, &bareAuthority)) return false;
        scheme = "https";
        rest = input;
    }

    const size_t authorityEnd = rest.find_first_of("/?#");
    const std::string authorityText =
        authorityEnd == std::string::npos ? rest : rest.substr(0, authorityEnd);
    const std::string tail =
        authorityEnd == std::string::npos ? std::string() : rest.substr(authorityEnd);

    Authority authority;
    if (!ParseAuthority(authorityText, false, &authority)) return false;

    std::string normalized = scheme + "://";
    normalized += authority.ipv6 ? "[" + authority.host + "]" : authority.host;
    if (!authority.port.empty()) normalized += ":" + authority.port;
    normalized += tail;

    if (normalized.size() > 4096) return false;

    out->url = normalized;
    out->display = MakeDisplay(authority, scheme);
    return !out->display.empty();
}
