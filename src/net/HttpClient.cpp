#include "HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace zaga {

namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
    if (length > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wide[0], length);
    }
    return wide;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrowed(length > 0 ? length - 1 : 0, '\0');
    if (length > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &narrowed[0], length, nullptr, nullptr);
    }
    return narrowed;
}

class Handle {
public:
    explicit Handle(HINTERNET handle) : _handle(handle) {}
    ~Handle() {
        if (_handle != nullptr) {
            WinHttpCloseHandle(_handle);
        }
    }
    HINTERNET get() const { return _handle; }
    explicit operator bool() const { return _handle != nullptr; }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

private:
    HINTERNET _handle;
};

// Turns a WinHTTP failure into something worth reading. The codes that matter here
// are the ones a bad address or an unplugged network actually produce; anything else
// falls back to the number, which is still better than silence.
std::string describe(DWORD code, const std::string& host) {
    switch (code) {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            return "No computer called \"" + host + "\" could be found on the network.";
        case ERROR_WINHTTP_CANNOT_CONNECT:
            return "Nothing answered at " + host + ". Check the portal is running and "
                   "that this machine can reach it.";
        case ERROR_WINHTTP_TIMEOUT:
            return "Timed out waiting for " + host + ".";
        case ERROR_WINHTTP_CONNECTION_ERROR:
            return "The connection to " + host + " was lost.";
        case ERROR_WINHTTP_SECURE_FAILURE:
            return "The HTTPS certificate for " + host + " could not be trusted.";
        default:
            return "Could not reach " + host + " (network error " +
                   std::to_string(code) + ").";
    }
}

HttpResponse send(const std::wstring& method,
                  const std::string& url,
                  const std::string& body,
                  const std::string& bearer) {
    HttpResponse response{false, 0, "", ""};

    std::wstring wideUrl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {0};
    wchar_t path[4096] = {0};
    wchar_t extra[2048] = {0};
    parts.lpszHostName = host;
    parts.dwHostNameLength = ARRAYSIZE(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = ARRAYSIZE(path);
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = ARRAYSIZE(extra);

    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
        response.error = "\"" + url + "\" is not a usable portal address. It should look "
                         "like http://192.168.1.20/zagatech.";
        return response;
    }

    std::string hostText = narrow(host);

    bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring fullPath = std::wstring(path) + extra;

    Handle session(WinHttpOpen(L"ZagaDeviceClient/0.1",
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        response.error = describe(GetLastError(), hostText);
        return response;
    }

    Handle connection(WinHttpConnect(session.get(), host, parts.nPort, 0));
    if (!connection) {
        response.error = describe(GetLastError(), hostText);
        return response;
    }

    Handle request(WinHttpOpenRequest(connection.get(), method.c_str(), fullPath.c_str(),
                                      nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) {
        response.error = describe(GetLastError(), hostText);
        return response;
    }

    std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json";
    if (!bearer.empty()) {
        headers += L"\r\nAuthorization: Bearer " + widen(bearer);
    }
    WinHttpAddRequestHeaders(request.get(), headers.c_str(), static_cast<DWORD>(-1),
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    BOOL sent = WinHttpSendRequest(
        request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(request.get(), nullptr)) {
        response.error = describe(GetLastError(), hostText);
        return response;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request.get(),
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);

    std::string received;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request.get(), &available) && available > 0) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), &chunk[0], available, &read)) {
            break;
        }
        received.append(chunk.data(), read);
    }

    response.completed = true;
    response.status = static_cast<int>(statusCode);
    response.body = received;
    return response;
}

}

HttpResponse HttpClient::get(const std::string& url, const std::string& bearer) {
    return send(L"GET", url, "", bearer);
}

HttpResponse HttpClient::post(const std::string& url,
                              const std::string& jsonBody,
                              const std::string& bearer) {
    return send(L"POST", url, jsonBody, bearer);
}

}
