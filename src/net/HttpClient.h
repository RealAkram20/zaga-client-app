#pragma once

#include <string>

namespace zaga {

struct HttpResponse {
    bool completed;
    int status;
    std::string body;

    // Why the call never reached a response, in words a technician standing at the
    // machine can act on. Empty when `completed` is true. A bare "could not reach the
    // portal" sends whoever is holding the laptop looking at the network when the real
    // answer is usually the address they typed.
    std::string error;
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
