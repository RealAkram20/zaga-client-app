#pragma once

#include <string>

namespace zaga {

struct HttpResponse {
    bool completed;
    int status;
    std::string body;
};

// Thin WinHTTP JSON caller. Sends and receives UTF-8 JSON with an optional bearer
// token. `completed` reports whether a response was received at all; `status` is
// the HTTP status code to judge success.
class HttpClient {
public:
    static HttpResponse get(const std::string& url, const std::string& bearer = "");

    static HttpResponse post(const std::string& url,
                             const std::string& jsonBody,
                             const std::string& bearer = "");
};

}
