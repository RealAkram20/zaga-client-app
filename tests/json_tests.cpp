#include <cstdio>
#include <string>

#include "Json.h"

using namespace zaga;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::printf("  ok   %s\n", label.c_str());
    } else {
        std::printf("  FAIL %s\n", label.c_str());
        ++g_failures;
    }
}

}

int main() {
    std::printf("enroll response\n");
    const std::string enroll =
        "{\"token\":\"1|N2bXh9AchvGAfLHIfg\",\"account_number\":\"ZG-40000\","
        "\"hmac_secret\":\"abab\",\"serial\":\"SN-1\",\"model\":\"Latitude\",\"name\":\"Reception\"}";
    Json parsed;
    check(Json::parse(enroll, parsed) && parsed.isObject(), "parses object");
    check(parsed.str("token") == "1|N2bXh9AchvGAfLHIfg", "token field");
    check(parsed.str("account_number") == "ZG-40000", "account field");
    check(parsed.str("hmac_secret") == "abab", "secret field");
    check(parsed.str("name") == "Reception", "name field");
    check(parsed.str("missing").empty(), "absent field is empty");

    std::printf("escapes and unicode\n");
    Json escaped;
    check(Json::parse("{\"m\":\"a\\\"b\\\\c\\/d\\ne\"}", escaped), "parses escapes");
    check(escaped.str("m") == "a\"b\\c/d\ne", "decodes escapes");
    Json unicode;
    check(Json::parse("{\"m\":\"caf\\u00e9\"}", unicode), "parses unicode escape");
    check(unicode.str("m") == "caf\xC3\xA9", "decodes to utf-8");

    std::printf("numbers, bools, nesting\n");
    Json token;
    check(Json::parse("{\"duration_days\":30,\"ok\":true,\"x\":null}", token), "parses mixed");
    check(token.get("duration_days") != nullptr && token.get("duration_days")->asNumber() == 30, "number value");
    check(token.get("ok") != nullptr && token.get("ok")->asBool(), "bool value");
    Json array;
    check(Json::parse("[{\"a\":1},{\"a\":2}]", array) && array.items().size() == 2, "parses array");

    std::printf("malformed input\n");
    Json bad;
    check(!Json::parse("{\"a\":}", bad), "rejects missing value");
    check(!Json::parse("{\"a\":1", bad), "rejects unclosed object");
    check(!Json::parse("nonsense", bad), "rejects garbage");
    check(!Json::parse("{\"a\":1}trailing", bad), "rejects trailing data");

    if (g_failures == 0) {
        std::printf("\nAll JSON checks passed.\n");
        return 0;
    }

    std::printf("\n%d JSON check(s) failed.\n", g_failures);
    return 1;
}
