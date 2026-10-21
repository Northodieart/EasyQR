#include "url.h"

#include <cstdio>
#include <string>
#include <vector>

struct Case {
    const char* input;
    bool valid;
    const char* url;
    const char* display;
};

int main() {
    const std::vector<Case> cases = {
        {"https://example.com", true, "https://example.com", "example.com"},
        {"  https://Example.COM/a?b=1#c  ", true, "https://example.com/a?b=1#c", "example.com"},
        {"HTTP://www.example.com:8080/x", true, "http://www.example.com:8080/x",
         "example.com:8080"},
        {"example.com/path", true, "https://example.com/path", "example.com"},
        {"www.example.com", true, "https://www.example.com", "example.com"},
        {"https://example.com/路径", true, "https://example.com/路径", "example.com"},
        {"8.8.8.8/dns", true, "https://8.8.8.8/dns", "8.8.8.8"},
        {"https://[::1]:8080/", true, "https://[::1]:8080/", "[::1]:8080"},
        {"http://intranet/page", true, "http://intranet/page", "intranet"},
        {"ftp://example.com", false, "", ""},
        {"javascript:alert(1)", false, "", ""},
        {"file:///C:/x", false, "", ""},
        {"http://user@evil.com", false, "", ""},
        {"http://", false, "", ""},
        {"hello world", false, "", ""},
        {"garbage", false, "", ""},
        {"https://例子.中国/", false, "", ""},
        {"https://example.com:99999", false, "", ""},
        {"https://example.com\\evil", false, "", ""},
        {"", false, "", ""},
        {"https://example.com/\"quote", false, "", ""},
    };

    int failures = 0;
    for (const Case& test : cases) {
        WebUrl url;
        const bool valid = TryMakeWebUrl(test.input, &url);
        const bool pass = valid == test.valid &&
                          (!valid || (url.url == test.url && url.display == test.display));
        if (!pass) {
            std::printf("FAIL input=[%s] valid=%d url=[%s] display=[%s]\n", test.input, valid,
                        url.url.c_str(), url.display.c_str());
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("url_tests: %zu cases passed\n", cases.size());
    }
    return failures == 0 ? 0 : 1;
}
