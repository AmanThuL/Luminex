#include "App/Model/Session/SessionProtocol.h"

#include "Engine/Asset/Model/JsonTokens.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

//======================================================================================================================
TEST_CASE("session result encoding returns a safe error for external invalid text",
          "[app][session-protocol][response-utf8]") {
    for (const std::string& result :
         {std::string("{\"path\":\"") + char(0xff) + "\"}", std::string("{broken")}) {
        const auto response = lmx::asset::JsonTokens::parse(encodeResult(4, result));
        REQUIRE(response);
        CHECK(response->root().find("id")->asUInt() == 4);
        CHECK(response->root().find("ok")->asBool() == false);
        CHECK(response->root().find("error")->find("code")->asString() == "failed");
        CHECK_FALSE(response->root().find("error")->find("message")->asString()->empty());
    }
    const auto continued = lmx::asset::JsonTokens::parse(encodeResult(5, R"({"ready":true})"));
    REQUIRE(continued);
    CHECK(continued->root().find("ok")->asBool() == true);
}

//======================================================================================================================
TEST_CASE("session error encoding replaces externally invalid UTF-8 safely",
          "[app][session-protocol][error-utf8]") {
    const auto response = lmx::asset::JsonTokens::parse(
        encodeError(6, SessionError::Unavailable, std::string("bad path ") + char(0xff)));
    REQUIRE(response);
    CHECK(response->root().find("id")->asUInt() == 6);
    CHECK(response->root().find("ok")->asBool() == false);
    CHECK(response->root().find("error")->find("code")->asString() == "unavailable");
    CHECK(response->root().find("error")->find("message")->asString()->find("UTF-8") !=
          std::string::npos);
    const auto continued =
        lmx::asset::JsonTokens::parse(encodeError(7, SessionError::Busy, "busy"));
    REQUIRE(continued);
    CHECK(continued->root().find("error")->find("message")->asString() == "busy");
}

//======================================================================================================================
TEST_CASE("session arguments reject unknown, repeated and unbounded members",
          "[app][session-protocol]") {
    const auto node = [](std::string text) {
        return lmx::asset::JsonTokens::parse(std::move(text))->root();
    };
    CHECK(validateSessionArguments(SessionCommand::Hello, node(R"({"name":"c","protocol":1})")));
    CHECK_FALSE(validateSessionArguments(SessionCommand::Hello,
                                         node(R"({"name":"c","protocol":1,"tier":"apply"})")));
    CHECK_FALSE(validateSessionArguments(SessionCommand::Hello,
                                         node(R"({"name":"c","name":"d","protocol":1})")));
    CHECK(validateSessionArguments(SessionCommand::QueryStatus, node("{}")));
    CHECK_FALSE(validateSessionArguments(SessionCommand::QueryStatus, node(R"({"note":1})")));
    CHECK_FALSE(validateSessionArguments(SessionCommand::QueryStatus, node("[]")));
    CHECK(validateSessionArguments(SessionCommand::QueryConsole, node("{}")));
    CHECK(validateSessionArguments(SessionCommand::QueryConsole, node(R"({"afterSequence":4})")));
    CHECK_FALSE(validateSessionArguments(SessionCommand::QueryConsole,
                                         node(R"({"afterSequence":4,"limit":1})")));
    CHECK_FALSE(validateSessionArguments(SessionCommand::QueryConsole,
                                         node(R"({"afterSequence":4,"afterSequence":9})")));
    CHECK(validateSessionArguments(SessionCommand::QueryLog, node("{}")));
    CHECK(validateSessionArguments(SessionCommand::QueryLog, node(R"({"afterSequence":4})")));
    CHECK_FALSE(validateSessionArguments(SessionCommand::QueryLog,
                                         node(R"({"afterSequence":4,"limit":1})")));
    CHECK(validateSessionArguments(SessionCommand::ProposeWithdraw, node(R"({"proposal":4})")));
    CHECK_FALSE(validateSessionArguments(SessionCommand::ProposeWithdraw,
                                         node(R"({"proposal":4,"client":"other"})")));

    CHECK(validateSessionArguments(
        SessionCommand::ProposeEdits,
        node(
            R"({"summary":"s","evidence":["a"],"edits":[{"subject":"look","field":"bloom","value":{"enabled":true}}]})")));
    CHECK_FALSE(validateSessionArguments(SessionCommand::ProposeEdits,
                                         node(R"({"summary":"s","edits":[],"accept":true})")));
    const auto nested = validateSessionArguments(
        SessionCommand::ProposeEdits,
        node(
            R"({"summary":"s","edits":[{"subject":"look","field":"bloom","value":{"enabled":true,"enabled":false}}]})"));
    REQUIRE_FALSE(nested);
    CHECK(nested.error() == "Duplicate argument enabled");
    CHECK_FALSE(validateSessionArguments(
        SessionCommand::ProposeEdits,
        node(R"({"summary":"s","edits":[{"subject":"a","subject":"b","field":"f","value":1}]})")));

    // settings.set is named by its setting, so only repetition is refused here.
    CHECK(validateSessionArguments(SessionCommand::SettingsSet, node(R"({"temporal":"off"})")));
    CHECK_FALSE(validateSessionArguments(SessionCommand::SettingsSet,
                                         node(R"({"temporal":"off","temporal":"taa"})")));

    const auto longName = validateSessionArguments(SessionCommand::QueryStatus,
                                                   node("{\"" + std::string(200, 'k') + "\":1}"));
    REQUIRE_FALSE(longName);
    CHECK(longName.error() == "Unknown argument (long name) for query.status");

    std::string wide = "{";
    for (size_t index = 0; index <= kMaxArgumentMembers; ++index)
        wide += std::string(index ? "," : "") + "\"k" + std::to_string(index) + "\":1";
    CHECK_FALSE(validateSessionArguments(SessionCommand::SettingsSet, node(wide + "}")));

    const auto deep = [&](size_t arrays) {
        return node(R"({"summary":"s","edits":)" + std::string(arrays, '[') +
                    std::string(arrays, ']') + "}");
    };
    CHECK(validateSessionArguments(SessionCommand::ProposeEdits, deep(kMaxArgumentDepth - 1)));
    CHECK_FALSE(validateSessionArguments(SessionCommand::ProposeEdits, deep(kMaxArgumentDepth)));
}

//======================================================================================================================
TEST_CASE("every session command accepts its documented argument names",
          "[app][session-protocol]") {
    const auto node = [](std::string text) {
        return lmx::asset::JsonTokens::parse(std::move(text))->root();
    };
    struct Documented {
        std::string_view command;
        std::vector<std::string_view> members;
    };
    // The guide's command reference, kept apart from the validator's own table. settings.set is
    // named by its setting and so accepts any single name.
    const std::vector<Documented> documented{
        {"hello", {"name", "protocol"}},
        {"query.status", {}},
        {"query.hierarchy", {}},
        {"query.selection", {}},
        {"query.camera", {}},
        {"query.settings", {}},
        {"query.readings", {}},
        {"query.performance", {}},
        {"query.graph", {}},
        {"query.console", {"afterSequence"}},
        {"query.proposals", {}},
        {"query.log", {"afterSequence"}},
        {"propose.edits", {"summary", "evidence", "edits"}},
        {"propose.withdraw", {"proposal"}},
        {"settings.set", {"temporal"}},
        {"debugview.set", {"topic", "value"}},
        {"scene.open", {"scene"}},
        {"measure.run", {"name", "warmup", "frames"}},
        {"capture.gpu", {}},
        {"capture.screenshot", {"name", "frames"}},
        {"capture.sequence", {"name", "frames", "warmup"}},
        {"graph.dump", {"name"}},
        {"plan.submit", {"summary", "steps"}},
    };
    CHECK(documented.size() == sessionCommands().size());
    for (const auto& spec : sessionCommands()) {
        INFO(spec.name);
        const auto found =
            std::find_if(documented.begin(), documented.end(),
                         [&](const auto& entry) { return entry.command == spec.name; });
        REQUIRE(found != documented.end());
        // Names are checked here; a missing or mistyped value is each command's own refusal.
        CHECK(validateSessionArguments(spec.command, node("{}")));
        std::string all = "{";
        for (const auto member : found->members) {
            INFO(member);
            CHECK(validateSessionArguments(spec.command,
                                           node("{\"" + std::string(member) + "\":1}")));
            all += std::string(all.size() > 1 ? "," : "") + "\"" + std::string(member) + "\":1";
        }
        CHECK(validateSessionArguments(spec.command, node(all + "}")));
        const auto unknown = validateSessionArguments(spec.command, node(R"({"undocumented":1})"));
        if (spec.command == SessionCommand::SettingsSet) {
            CHECK(unknown);
        } else {
            REQUIRE_FALSE(unknown);
            CHECK(unknown.error() == "Unknown argument undocumented for " + std::string(spec.name));
        }
        CHECK_FALSE(validateSessionArguments(spec.command, node("[]")));
    }
}
