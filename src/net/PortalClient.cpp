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

// -1 when the field is missing or out of range — an old portal, not zero grace.
int graceDaysOf(const Json& json) {
    const Json* grace = json.get("grace_days");
    if (grace == nullptr || grace->type() != Json::Type::Number) {
        return -1;
    }
    double days = grace->asNumber();
    if (days < 0 || days > 255) {
        return -1;
    }
    return static_cast<int>(days);
}

}

PortalClient::PortalClient(const std::string& baseUrl) : _baseUrl(baseUrl) {
    while (!_baseUrl.empty() && _baseUrl.back() == '/') {
        _baseUrl.pop_back();
    }

    // "192.168.1.20/zagatech" is what someone types when asked for an address, and
    // WinHTTP rejects it outright for want of a scheme — indistinguishable, from the
    // outside, from the portal being down. Assume http rather than fail: the portals
    // these devices enrol against are plain http on a local network, and anyone using
    // https will have typed it.
    if (!_baseUrl.empty() && _baseUrl.find("://") == std::string::npos) {
        _baseUrl = "http://" + _baseUrl;
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
        result.message = response.error;
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
    result.graceDays = graceDaysOf(json);
    if (result.graceDays < 0) {
        result.graceDays = 0;
    }
    return result;
}

HeartbeatResult PortalClient::heartbeat(const std::string& deviceToken,
                                        const std::string& status,
                                        const std::string& agentVersion) {
    HeartbeatResult result{};

    std::string body = "{" + field("status", status) + "," +
                       field("agent_version", agentVersion) + "}";

    HttpResponse response = HttpClient::post(url("/api/device/heartbeat"), body, deviceToken);
    if (!response.completed || response.status != 200) {
        return result;
    }

    result.ok = true;

    Json json;
    if (Json::parse(response.body, json)) {
        result.graceDays = graceDaysOf(json);
    }
    return result;
}

TokenResult PortalClient::fetchToken(const std::string& deviceToken) {
    TokenResult result{};

    HttpResponse response = HttpClient::get(url("/api/device/token"), deviceToken);
    if (!response.completed) {
        result.message = response.error;
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
