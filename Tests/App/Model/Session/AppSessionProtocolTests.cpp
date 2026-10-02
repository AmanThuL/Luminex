#include "App/Model/Session/SessionProtocol.h"

#include "Engine/Asset/Model/JsonTokens.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("session request owns arguments and preserves unsigned identities",
          "[app][session-protocol]") {
    const auto request = decodeRequest(
        R"({"id":18446744073709551615,"command":"query.status","args":{"note":"\ud83d\ude00"}})");
    REQUIRE(request);
    CHECK(request->id == std::numeric_limits<uint64_t>::max());
    CHECK(request->command == "query.status");
    REQUIRE(request->args.isObject());
    REQUIRE(request->args.find("note"));
    CHECK(request->args.find("note")->asString() == "😀");
    const auto copy = *request;
    CHECK(copy.args.find("note")->asString() == "😀");

    const auto absent = decodeRequest(std::string(R"({"id":0,"command":"hello"})") + "\n");
    REQUIRE(absent);
    CHECK(absent->args.isObject());
    CHECK(absent->args.size() == 0);
    const auto empty = decodeRequest(R"({"id":1,"command":"hello","args":{}})");
    REQUIRE(empty);
    CHECK(empty->args.size() == 0);
    const std::string prefix = R"({"id":2,"command":"hello","args":{"pad":")";
    const std::string suffix = R"("}})";
    const auto boundary = decodeRequest(
        prefix + std::string(kMaxLineBytes - prefix.size() - suffix.size(), 'x') + suffix);
    REQUIRE(boundary);
    CHECK(boundary->args.find("pad")->asString()->size() ==
          kMaxLineBytes - prefix.size() - suffix.size());
    const auto unicode = decodeRequest(R"({"id":3,"command":"hello","args":{"name":"中文😀"}})");
    REQUIRE(unicode);
    CHECK(unicode->args.find("name")->asString() == "中文😀");
}

//======================================================================================================================
TEST_CASE("session envelope keeps unknown command identity for invalid response",
          "[app][session-protocol]") {
    const auto request =
        decodeRequestEnvelope(R"({"id":18446744073709551615,"command":"query.future"})");
    REQUIRE(request);
    CHECK(request->id == std::numeric_limits<uint64_t>::max());
    CHECK(request->command == "query.future");
    CHECK_FALSE(decodeRequest(R"({"id":18446744073709551615,"command":"query.future"})"));
}

//======================================================================================================================
TEST_CASE("session request rejects malformed or ambiguous input with named errors",
          "[app][session-protocol]") {
    const std::vector<std::pair<std::string, std::string_view>> invalid = {
        {"{", "JSON"},
        {"[]", "object"},
        {R"({"command":"hello"})", "id"},
        {R"({"id":-1,"command":"hello"})", "id"},
        {R"({"id":1.0,"command":"hello"})", "id"},
        {R"({"id":18446744073709551616,"command":"hello"})", "id"},
        {R"({"id":1})", "command"},
        {R"({"id":1,"command":3})", "command"},
        {R"({"id":1,"command":"missing.command"})", "command"},
        {R"({"id":1,"command":"hello","args":[]})", "args"},
        {R"({"id":1,"id":2,"command":"hello"})", "id"},
        {R"({"id":1,"command":"hello","args":{},"args":{}})", "fields"},
        {R"({"id":1,"command":"hello","unexpected":true})", "unexpected"},
        {R"({"id":1,"command":"hello","args":{},"extra":true})", "fields"},
        {std::string(R"({"id":1,"command":"hello"})") + "\n{}", "line"},
        {std::string(kMaxLineBytes + 1, 'x'), "line"},
    };
    for (const auto& [line, field] : invalid) {
        INFO(field);
        const auto result = decodeRequest(line);
        REQUIRE_FALSE(result);
        CHECK(result.error().find(field) != std::string::npos);
    }
    for (const std::string& raw :
         {std::string("\x80", 1), std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3),
          std::string("\xf4\x90\x80\x80", 4), std::string("\xe2\x82", 2)}) {
        const auto result =
            decodeRequest("{\"id\":1,\"command\":\"hello\",\"args\":{\"x\":\"" + raw + "\"}}");
        REQUIRE_FALSE(result);
        CHECK(result.error().find("UTF-8") != std::string::npos);
    }
    std::string manyFields = R"({"id":1,"command":"hello","args":{})";
    for (size_t i = 0; i < 60002; ++i)
        manyFields += ",\"f" + std::to_string(i) + "\":0";
    manyFields += '}';
    REQUIRE(manyFields.size() <= kMaxLineBytes);
    const auto many = decodeRequest(std::move(manyFields));
    REQUIRE_FALSE(many);
    CHECK(many.error().find("fields") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("session responses are one valid JSON line with exact identity and escaped messages",
          "[app][session-protocol]") {
    const auto result = encodeResult(std::numeric_limits<uint64_t>::max(), R"({"value":"ok"})");
    CHECK(result.back() == '\n');
    CHECK(result.find('\n') == result.size() - 1);
    const auto parsed = lmx::asset::JsonTokens::parse(result);
    REQUIRE(parsed);
    CHECK(parsed->root().find("id")->asUInt() == std::numeric_limits<uint64_t>::max());
    CHECK(parsed->root().find("ok")->asBool() == true);
    CHECK(parsed->root().find("result")->find("value")->asString() == "ok");
    const auto spaced =
        encodeResult(9, "{ \"value\" : \"a b \\\"c\\\"\", \"nested\" : [ 1, 2 ] }\n");
    CHECK(spaced.find('\n') == spaced.size() - 1);
    const auto parsedSpaced = lmx::asset::JsonTokens::parse(spaced);
    REQUIRE(parsedSpaced);
    CHECK(parsedSpaced->root().find("result")->find("value")->asString() == "a b \"c\"");
    CHECK(parsedSpaced->root().find("result")->find("nested")->at(1).asUInt() == 2);

    const auto error = encodeError(7, SessionError::Unavailable, "line\n\"quote\" 😀");
    CHECK(error.find('\n') == error.size() - 1);
    const auto decoded = lmx::asset::JsonTokens::parse(error);
    REQUIRE(decoded);
    CHECK(decoded->root().find("id")->asUInt() == 7);
    CHECK(decoded->root().find("ok")->asBool() == false);
    CHECK(decoded->root().find("error")->find("code")->asString() == "unavailable");
    CHECK(decoded->root().find("error")->find("message")->asString() == "line\n\"quote\" 😀");
    const std::array codes = {
        std::pair{SessionError::Protocol, "protocol"},
        std::pair{SessionError::Tier, "tier"},
        std::pair{SessionError::Denied, "denied"},
        std::pair{SessionError::Unavailable, "unavailable"},
        std::pair{SessionError::Invalid, "invalid"},
        std::pair{SessionError::Busy, "busy"},
        std::pair{SessionError::Failed, "failed"},
        std::pair{SessionError::Cancelled, "cancelled"},
    };
    for (const auto& [code, name] : codes) {
        const auto response = lmx::asset::JsonTokens::parse(encodeError(0, code, "x"));
        REQUIRE(response);
        CHECK(response->root().find("error")->find("code")->asString() == name);
    }
}
