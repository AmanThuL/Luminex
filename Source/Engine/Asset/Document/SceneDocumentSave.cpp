//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentSave.cpp
/// @brief Preflights immutable companions and stages, verifies and rolls back document saves.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Document/SceneDocumentSaveInternal.h"

#include "Core/IO/File.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/SceneDocumentWriteInternal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <span>
#include <system_error>

#include <unistd.h>

namespace lmx::asset {
namespace {

//======================================================================================================================
AssetError ioError(const std::filesystem::path& path, std::string_view message) {
    return {AssetErrorCode::Io, path.string() + ": " + std::string(message)};
}

//======================================================================================================================
AssetResult<void> writableTarget(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory)
        return {};
    if (ec)
        return std::unexpected(ioError(path, ec.message()));
    if (status.type() == std::filesystem::file_type::not_found)
        return {};
    if (!std::filesystem::is_regular_file(status))
        return std::unexpected(ioError(path, "target is not a regular file"));
    constexpr auto writeBits = std::filesystem::perms::owner_write |
                               std::filesystem::perms::group_write |
                               std::filesystem::perms::others_write;
    if ((status.permissions() & writeBits) == std::filesystem::perms::none ||
        access(path.c_str(), W_OK) != 0)
        return std::unexpected(ioError(path, "target is read-only"));
    return {};
}

//======================================================================================================================
// An existing companion may be replaced only when the existing target document names it; a stray
// file beside a new Save As destination is somebody else's data.
AssetResult<void> ownedCompanion(const std::filesystem::path& path,
                                 const std::filesystem::path& binPath) {
    std::error_code ec;
    if (!std::filesystem::exists(binPath, ec) && !ec)
        return {};
    // Only the validated animation URI matters; a damaged animation may still be replaced.
    if (const auto bytes = readWholeFile(path)) {
        const auto buffer = sceneDocumentBufferPath(
            std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), path);
        if (buffer && *buffer && std::filesystem::equivalent(**buffer, binPath, ec) && !ec)
            return {};
    }
    return std::unexpected(ioError(binPath, "companion file exists and is not referenced by the "
                                            "target document; refusing to overwrite it"));
}

//======================================================================================================================
AssetResult<void> writeBytes(const std::filesystem::path& path, const void* data, size_t size) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return std::unexpected(ioError(path, "cannot open staged output"));
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    file.flush();
    const bool good = file.good();
    file.close();
    if (!good || file.fail())
        return std::unexpected(ioError(path, "cannot write staged output"));
    return {};
}

struct Companion {
    std::filesystem::path relative;
    std::span<const std::byte> bytes;
    std::string hash;
    bool present = false;
};

//======================================================================================================================
AssetResult<void> checkCompanion(Companion& companion, const std::filesystem::path& parent) {
    const auto path = parent / companion.relative;
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found)
        return {};
    if (ec)
        return std::unexpected(ioError(path, ec.message()));
    if (!std::filesystem::is_regular_file(status))
        return std::unexpected(ioError(path, "immutable companion is not a regular file"));
    const auto bytes = readWholeFile(path);
    if (!bytes || sha256Hex(*bytes) != companion.hash)
        return std::unexpected(
            ioError(path, "immutable companion SHA-256 does not match the model"));
    companion.present = true;
    return {};
}

//======================================================================================================================
AssetResult<void> checkTextureFolder(const std::filesystem::path& folder,
                                     const std::vector<Companion>& companions) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(folder, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found)
        return {};
    if (ec)
        return std::unexpected(ioError(folder, ec.message()));
    if (!std::filesystem::is_directory(status))
        return std::unexpected(ioError(folder, "texture folder is not a regular directory"));
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end;
         it.increment(ec)) {
        const auto name = it->path().filename();
        const bool known = std::any_of(companions.begin(), companions.end(), [&](const auto& c) {
            return c.relative.parent_path() == folder.filename() && c.relative.filename() == name;
        });
        if (!known)
            return std::unexpected(ioError(it->path(), "foreign file in the texture folder"));
    }
    if (ec)
        return std::unexpected(ioError(folder, ec.message()));
    const bool needsWrite = std::any_of(companions.begin(), companions.end(), [](const auto& c) {
        return c.relative.has_parent_path() && !c.present;
    });
    constexpr auto writeBits = std::filesystem::perms::owner_write |
                               std::filesystem::perms::group_write |
                               std::filesystem::perms::others_write;
    if (needsWrite && ((status.permissions() & writeBits) == std::filesystem::perms::none ||
                       access(folder.c_str(), W_OK) != 0))
        return std::unexpected(ioError(folder, "texture folder is read-only"));
    return {};
}

} // namespace

//======================================================================================================================
AssetResult<void> detail::saveSceneDocumentWithRename(const SceneDocument& input,
                                                      const std::filesystem::path& path,
                                                      const DocumentRename& rename) {
    if (auto valid = validateSceneDocumentModel(input); !valid)
        return valid;
    const auto doc = sceneDocumentSaveForm(input);
    if (auto valid = validateSceneDocumentModel(doc); !valid)
        return valid;
    if (path.extension() != ".gltf")
        return std::unexpected(ioError(path, "scene document must use the .gltf extension"));
    auto binPath = path;
    binPath.replace_extension(".bin");
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    const auto bufferUri = binPath.filename().string();
    const auto bin = sceneDocumentBuffer(doc);
    const auto geometry = sceneDocumentGeometry(doc);
    std::vector<Companion> companions;
    if (doc.content) {
        companions.push_back({geometryUri(bufferUri), geometry, doc.content->geometrySha256});
        for (const auto& image : doc.content->images)
            companions.push_back({imageUri(bufferUri, image), image.file, image.sha256});
    }
    // Validate every immutable destination before creating any staging directory or file.
    for (auto& companion : companions)
        if (auto checked = checkCompanion(companion, parent); !checked)
            return checked;
    const auto textureFolder = parent / (path.stem().string() + ".textures");
    if (doc.content)
        if (auto checked = checkTextureFolder(textureFolder, companions); !checked)
            return checked;
    std::vector<std::filesystem::path> targets{path};
    if (!bin.empty()) {
        if (auto owned = ownedCompanion(path, binPath); !owned)
            return owned;
        targets.push_back(binPath);
    }
    const size_t replaceable = targets.size();
    for (const auto& target : targets)
        if (auto writable = writableTarget(target); !writable)
            return writable;
    for (const auto& companion : companions)
        if (!companion.present)
            targets.push_back(parent / companion.relative);
    std::error_code ec;
    const auto permissions = std::filesystem::status(parent, ec).permissions();
    constexpr auto writeBits = std::filesystem::perms::owner_write |
                               std::filesystem::perms::group_write |
                               std::filesystem::perms::others_write;
    if (ec || (permissions & writeBits) == std::filesystem::perms::none ||
        access(parent.c_str(), W_OK) != 0)
        return std::unexpected(ioError(parent, "output directory is not writable"));
    static std::atomic<uint64_t> sequence{0};
    const auto unique =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(sequence++);
    const auto staging = parent / (".lmx-save-" + unique + ".tmp");
    if (!std::filesystem::create_directory(staging, ec))
        return std::unexpected(ioError(staging, ec.message()));
    const auto cleanup = [&] {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
    };
    const auto json = sceneDocumentJson(doc, bufferUri);
    auto written = writeBytes(staging / path.filename(), json.data(), json.size());
    if (written && !bin.empty())
        written = writeBytes(staging / binPath.filename(), bin.data(), bin.size());
    for (const auto& companion : companions) {
        if (!written)
            break;
        if (companion.relative.has_parent_path()) {
            std::filesystem::create_directories(staging / companion.relative.parent_path(), ec);
            if (ec) {
                written = std::unexpected(ioError(companion.relative, ec.message()));
                break;
            }
        }
        written = writeBytes(staging / companion.relative, companion.bytes.data(),
                             companion.bytes.size());
    }
    if (!written) {
        cleanup();
        return written;
    }
    const auto checked = readSceneDocument(staging / path.filename());
    if (!checked) {
        cleanup();
        return std::unexpected(checked.error());
    }
    std::vector<bool> backedUp(targets.size()), installed(targets.size());
    bool createdTextureFolder = false;
    const auto rollback = [&](AssetError error) -> AssetResult<void> {
        bool restored = true;
        for (size_t i = 0; i < targets.size(); ++i) {
            std::error_code recovery;
            if (installed[i]) {
                std::filesystem::remove(targets[i], recovery);
                if (recovery) {
                    restored = false;
                    error.message += "; rollback remove: " + recovery.message();
                }
            }
            if (backedUp[i]) {
                rename(staging / (std::to_string(i) + ".bak"), targets[i], recovery);
                if (recovery) {
                    restored = false;
                    error.message += "; rollback restore: " + recovery.message();
                }
            }
        }
        if (createdTextureFolder) {
            std::error_code recovery;
            std::filesystem::remove(textureFolder, recovery);
            if (recovery) {
                restored = false;
                error.message += "; rollback directory: " + recovery.message();
            }
        }
        if (restored)
            cleanup();
        else
            error.message += "; preserved backups at " + staging.string();
        return std::unexpected(std::move(error));
    };
    for (size_t i = 0; i < targets.size(); ++i) {
        const bool exists = std::filesystem::exists(targets[i], ec);
        if (ec)
            return rollback(ioError(targets[i], ec.message()));
        if (exists) {
            if (i >= replaceable)
                return rollback(ioError(targets[i], "companion appeared during save"));
            rename(targets[i], staging / (std::to_string(i) + ".bak"), ec);
            if (ec)
                return rollback(ioError(targets[i], ec.message()));
            backedUp[i] = true;
        }
    }
    for (size_t i = 0; i < targets.size(); ++i) {
        const auto relative = targets[i].lexically_relative(parent);
        if (relative.has_parent_path() && !std::filesystem::exists(textureFolder, ec)) {
            if (ec)
                return rollback(ioError(textureFolder, ec.message()));
            createdTextureFolder = std::filesystem::create_directory(textureFolder, ec);
            if (!createdTextureFolder)
                return rollback(ioError(textureFolder, ec.message()));
        }
        rename(staging / relative, targets[i], ec);
        if (ec)
            return rollback(ioError(targets[i], ec.message()));
        installed[i] = true;
    }
    cleanup();
    return {};
}

//======================================================================================================================
SceneDocument sceneDocumentSaveForm(const SceneDocument& doc) {
    auto saved = doc;
    if (saved.schemaVersion == 1) {
        saved.schemaVersion = kSceneDocumentSchema;
        for (auto& node : saved.nodes) {
            node.mobility = node.light ? DocMobility::Movable : DocMobility::Static;
            for (auto& override : node.overrides)
                override.mobility.reset();
        }
    }
    return saved;
}

//======================================================================================================================
AssetResult<void> saveSceneDocument(const SceneDocument& doc, const std::filesystem::path& path) {
    return detail::saveSceneDocumentWithRename(
        doc, path,
        [](const std::filesystem::path& from, const std::filesystem::path& to,
           std::error_code& error) { std::filesystem::rename(from, to, error); });
}

} // namespace lmx::asset
