#include <catch2/catch_test_macros.hpp>

#include "Engine/Asset/Model/JsonTokens.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

using namespace lmx::asset;

//======================================================================================================================
TEST_CASE("JSON object iteration preserves decoded keys and source order", "[asset][json-tokens]") {
    const auto parsed = JsonTokens::parse(R"({"b":2,"a/b":false,"\u0063":{"x":1}})");
    REQUIRE(parsed);
    const auto root = parsed->root();
    REQUIRE(root.memberName(0) == "b");
    REQUIRE(root.memberName(1) == "a/b");
    REQUIRE(root.memberName(2) == "c");
    REQUIRE(root.memberValue(0).asUInt() == 2);
    REQUIRE(root.memberValue(1).path() == "/a~1b");
    REQUIRE(root.memberValue(2).find("x")->asUInt() == 1);
}

//======================================================================================================================
TEST_CASE("JSON array iteration preserves order and nested pointer paths", "[asset][json-tokens]") {
    const auto parsed = JsonTokens::parse(R"({"edits":[{"value":[1,2]},true,null]})");
    REQUIRE(parsed);
    const auto nodes = parsed->root().find("edits")->elements();
    REQUIRE(nodes.size() == 3);
    CHECK(nodes[0].path() == "/edits/0");
    CHECK(nodes[0].find("value")->at(1).asUInt() == 2);
    CHECK(nodes[1].asBool() == true);
    CHECK(nodes[2].isNull());
}

//======================================================================================================================
TEST_CASE("padded JSON tokenizer errors stay inside the original text", "[asset][json-tokens]") {
    for (const std::string source : {"[", "{", "{\"a\":", "[1"}) {
        const auto parsed = JsonTokens::parse(source);
        REQUIRE_FALSE(parsed);
        REQUIRE(parsed.error().message.contains("byte " + std::to_string(source.size())));
    }
}

//======================================================================================================================
TEST_CASE("JSON tokens traverse nested members and preserve pointer paths",
          "[asset][json-tokens]") {
    const auto parsed = JsonTokens::parse(R"({"extensions":{"LMX_scene":{"look":{"bloom":{
        "threshold":"bad", "values":[1.25, false, null, {}]}}}},"a/b~c":42})");
    REQUIRE(parsed);
    const auto root = parsed->root();
    REQUIRE(root.isObject());
    REQUIRE(root.path().empty());
    REQUIRE(root.size() == 2);
    REQUIRE_FALSE(root.find("missing"));
    const auto bloom = root.find("extensions")->find("LMX_scene")->find("look")->find("bloom");
    REQUIRE(bloom);
    const auto threshold = bloom->find("threshold");
    REQUIRE(threshold);
    REQUIRE(threshold->isString());
    REQUIRE(threshold->asString() == "bad");
    REQUIRE(threshold->path() == "/extensions/LMX_scene/look/bloom/threshold");
    const auto mismatch = threshold->asFloat();
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.error().contains(threshold->path()));
    REQUIRE(mismatch.error().contains("number"));
    const auto values = bloom->find("values");
    REQUIRE(values->isArray());
    REQUIRE(values->size() == 4);
    REQUIRE(values->at(0).isNumber());
    REQUIRE(values->at(0).asFloat() == 1.25f);
    REQUIRE(values->at(0).asDouble() == 1.25);
    REQUIRE(values->at(1).isBool());
    REQUIRE(values->at(1).asBool() == false);
    REQUIRE(values->at(1).path() == "/extensions/LMX_scene/look/bloom/values/1");
    REQUIRE(values->at(2).isNull());
    REQUIRE(values->at(3).size() == 0);
    REQUIRE(root.find("a/b~c")->path() == "/a~1b~0c");
}

//======================================================================================================================
TEST_CASE("JSON tokens report unterminated string byte offsets", "[asset][json-tokens]") {
    const auto parsed = JsonTokens::parse(R"({"name":"broken})");
    REQUIRE_FALSE(parsed);
    REQUIRE(parsed.error().code == AssetErrorCode::Malformed);
    REQUIRE(parsed.error().message.contains("byte 8"));
}

//======================================================================================================================
TEST_CASE("JSON nodes keep owned immutable bytes across moves and destruction",
          "[asset][json-tokens]") {
    const auto node = [] {
        std::string source = R"({"name":"original"})";
        auto parsed = JsonTokens::parse(source);
        REQUIRE(parsed);
        source.assign("changed");
        const auto result = *parsed->root().find("name");
        auto moved = std::move(*parsed);
        REQUIRE(moved.root().find("name")->asString() == "original");
        return result;
    }();
    REQUIRE(node.asString() == "original");
    REQUIRE(node.path() == "/name");
}

//======================================================================================================================
TEST_CASE("JSON strings decode escapes and Unicode including surrogate pairs",
          "[asset][json-tokens]") {
    const auto parsed =
        JsonTokens::parse(R"({"\u006eame":"\"\\\/\b\f\n\r\t\u0000\u4e2d\ud83d\ude00"})");
    REQUIRE(parsed);
    const auto name = parsed->root().find("name");
    REQUIRE(name);
    REQUIRE(name->path() == "/name");
    const std::string expected = std::string("\"\\/\b\f\n\r\t\0", 9) + "中😀";
    REQUIRE(name->asString() == expected);
}

//======================================================================================================================
TEST_CASE("JSON token validation rejects incomplete grammar and numeric tokens",
          "[asset][json-tokens]") {
    for (const std::string source : {"",
                                     "{} []",
                                     "[1,]",
                                     "[,1]",
                                     "[1 2]",
                                     "{\"a\":1,}",
                                     "{\"a\" 1}",
                                     "{\"a\":}",
                                     "{\"a\":1 \"b\":2}",
                                     "{\"a\"::1}",
                                     "[01]",
                                     "[1junk]",
                                     "[1.]",
                                     "[1e]",
                                     "[--1]",
                                     "[truejunk]",
                                     "[nul]",
                                     "[NaN]",
                                     "[Infinity]",
                                     "\"line\nbreak\"",
                                     R"("\ud800")",
                                     R"("\udc00")"}) {
        INFO(source);
        const auto parsed = JsonTokens::parse(source);
        REQUIRE_FALSE(parsed);
        REQUIRE(parsed.error().code == AssetErrorCode::Malformed);
        REQUIRE(parsed.error().message.contains("byte"));
    }
    REQUIRE_FALSE(JsonTokens::parse(std::string("{}\0[]", 5)));
}

//======================================================================================================================
TEST_CASE("JSON scalar roots and numeric range errors remain explicit", "[asset][json-tokens]") {
    const auto largest = JsonTokens::parse("18446744073709551615");
    REQUIRE(largest);
    REQUIRE(largest->root().asUInt() == std::numeric_limits<uint64_t>::max());
    const auto negativeZero = JsonTokens::parse("-0");
    REQUIRE(negativeZero);
    REQUIRE(std::bit_cast<uint32_t>(*negativeZero->root().asFloat()) == 0x80000000u);
    REQUIRE_FALSE(negativeZero->root().asUInt());
    for (const std::string source : {"-1", "1.5", "1e2", "18446744073709551616"}) {
        const auto parsed = JsonTokens::parse(source);
        REQUIRE(parsed);
        REQUIRE_FALSE(parsed->root().asUInt());
    }
    for (const std::string source : {"1e1000", "1e-1000"}) {
        const auto parsed = JsonTokens::parse(source);
        REQUIRE(parsed);
        REQUIRE_FALSE(parsed->root().asFloat());
        REQUIRE_FALSE(parsed->root().asDouble());
    }
    const auto truth = JsonTokens::parse("true");
    REQUIRE(truth);
    REQUIRE(truth->root().asBool() == true);
    REQUIRE_FALSE(truth->root().asString());
    REQUIRE_FALSE(truth->root().asDouble());
    REQUIRE_FALSE(truth->root().asUInt());
    const auto string = JsonTokens::parse(R"("true")");
    REQUIRE(string);
    REQUIRE_FALSE(string->root().asBool());
}
