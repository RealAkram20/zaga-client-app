#include "PortalClient.h"

#include "HardwareInfo.h"
#include "HttpClient.h"
#include "Json.h"

namespace zaga {

namespace {

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    for (char c : value) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}

std::string field(const std::string& name, const std::string& value) {
    return "\"" + name + "\":\"" + jsonEscape(value) + "\"";
}

std::string messageOf(const std::string& body, const std::string& fallback) {
    Json json;
    if (Json::parse(body, json) && json.isObject()) {
        std::string message = json.str("message");
        if (!message.empty()) {
            return message;
        }
    }
    return fallback;
}

}

PortalClient::PortalClient(const std::string& baseUrl) : _baseUrl(baseUrl) {
    while (!_baseUrl.empty() && _baseUrl.back() == '/') {
        _baseUrl.pop_back();
    }
}

std::string PortalClient::url(const std::string& path) const {
    return _baseUrl + path;
}

EnrollResult PortalClient::enroll(const std::string& enrollmentCode, const std::string& agentVersion) {
    EnrollResult result{};

    // Report what this machine actually is. The portal treats the firmware as
    // authoritative, so an operator never has to type a serial correctly.
    HardwareInfo hardware = Hardware::detect();

    std::string body = "{" + field("code", enrollmentCode) + "," +
                       field("agent_version", agentVersion) + "," +
                       field("serial", hardware.serial) + "," +
                       field("model", hardware.model) + "," +
                       field("manufacturer", hardware.manufacturer) + "," +
                       field("hostname", hardware.hostname) + "}";

    HttpResponse response = HttpClient::post(url("/api/device/enroll"), body);
    if (!response.completed) {
        result.message = "Could not reach the portal.";
        return result;
    }

    if (response.status != 200) {
        result.message = messageOf(response.body, "Enrollment was rejected.");
        return result;
    }

    Json json;
    if (!Json::parse(response.body, json) || json.str("token").empty()) {
        result.message = "The portal returned an unexpected response.";
        return result;
    }

    result.ok = true;
    result.token = json.str("token");
    result.accountNumber = json.str("account_number");
    result.hmacSecret = json.str("hmac_secret");
    result.serial = json.str("serial");
    result.model = json.str("model");
    result.name = json.str("name");
    return result;
}

bool PortalClient::heartbeat(const std::string& deviceToken,
                             const std::string& status,
                             const std::string& agentVersion) {
    std::string body = "{" + field("status", status) + "," +
                       field("agent_version", agentVersion) + "}";

    HttpResponse response = HttpClient::post(url("/api/device/heartbeat"), body, deviceToken);
    return response.completed && response.status == 200;
}

TokenResult PortalClient::fetchToken(const std::string& deviceToken) {
    TokenResult result{};

    HttpResponse response = HttpClient::get(url("/api/device/token"), deviceToken);
    if (!response.completed) {
        result.message = "Could not reach the portal.";
        return result;
    }

    if (response.status != 200) {
        result.message = messageOf(response.body, "No token is available.");
        return result;
    }

    Json json;
    if (!Json::parse(response.body, json) || json.str("token").empty()) {
        result.message = "The portal returned an unexpected response.";
        return result;
    }

    result.ok = true;
    result.token = json.str("token");
    return result;
}

}
