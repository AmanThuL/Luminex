//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentUri.cpp
/// @brief Converts URI paths without confusing encoded filenames with traversal or URI syntax.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Document/DocumentUri.h"

#include <filesystem>

namespace lmx::asset::detail {
namespace {

//======================================================================================================================
bool unreserved(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '.' || c == '_' || c == '~';
}

//======================================================================================================================
int hexDigit(unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

} // namespace

//======================================================================================================================
std::string encodeDocumentUri(std::string_view path) {
    constexpr std::string_view kHex = "0123456789ABCDEF";
    std::string uri;
    for (unsigned char c : path) {
        if (unreserved(c) || c == '/')
            uri += static_cast<char>(c);
        else {
            uri += '%';
            uri += kHex[c >> 4];
            uri += kHex[c & 15];
        }
    }
    return uri;
}

//======================================================================================================================
AssetResult<std::string> decodeDocumentUri(std::string_view uri, std::string_view pointer) {
    const auto invalid = [&](std::string_view reason) -> AssetResult<std::string> {
        return std::unexpected(
            AssetError{AssetErrorCode::Malformed,
                       "JSON pointer '" + std::string(pointer) + "': " + std::string(reason)});
    };
    std::string decoded;
    for (size_t i = 0; i < uri.size(); ++i) {
        const unsigned char c = uri[i];
        if (c == '%') {
            if (i + 2 >= uri.size() || hexDigit(uri[i + 1]) < 0 || hexDigit(uri[i + 2]) < 0)
                return invalid("malformed percent escape in relative file URI");
            decoded += static_cast<char>((hexDigit(uri[i + 1]) << 4) | hexDigit(uri[i + 2]));
            i += 2;
        } else {
            if (!unreserved(c) && c != '/' &&
                std::string_view("!$&'()*+,;=@").find(c) == std::string_view::npos)
                return invalid("relative file URI contains an unescaped or reserved character");
            decoded += static_cast<char>(c);
        }
    }
    if (decoded.empty() || decoded.find('\0') != std::string::npos ||
        decoded.find('\\') != std::string::npos || decoded.find(':') != std::string::npos)
        return invalid("expected a relative file path without NUL, backslash or scheme");
    const std::filesystem::path path(decoded);
    if (path.is_absolute())
        return invalid("absolute file paths are not supported");
    for (const auto& part : path)
        if (part == "..")
            return invalid("relative file path must not traverse a parent directory");
    return decoded;
}

} // namespace lmx::asset::detail
