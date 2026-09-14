/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/core/File.h>
#include <openrct2/core/FileSystem.hpp>
#include <openrct2/object/ObjectRepository.h>
#include <openrct2/platform/Platform.h>

#include <array>
#include <chrono>
#include <vector>

using namespace OpenRCT2;

TEST(platform, sanitise_filename)
{
    ASSERT_EQ("normal-filename.png", Platform::SanitiseFilename("normal-filename.png"));
    ASSERT_EQ("utf🎱", Platform::SanitiseFilename("utf🎱"));
    ASSERT_EQ("forbidden_char", Platform::SanitiseFilename("forbidden/char"));
    ASSERT_EQ("non trimmed", Platform::SanitiseFilename(" non trimmed "));
#ifndef _WIN32
    ASSERT_EQ("forbidden_\\:\"|?*chars", Platform::SanitiseFilename("forbidden/\\:\"|?*chars"));
#else
    ASSERT_EQ("forbidden_______chars", Platform::SanitiseFilename("forbidden/\\:\"|?*chars"));
#endif
}

TEST(platform, packed_object_storage_sanitises_names_and_preserves_collision_files)
{
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    const auto root = fs::current_path()
        / ("object-path-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(fs::create_directory(root));
    auto env = CreatePlatformEnvironment();
    env->SetBasePath(DirBase::user, root.u8string());
    env->SetBasePath(DirBase::cache, root.u8string());
    const fs::path objectDirectory = env->GetDirectoryPath(DirBase::user, DirId::objects);
    struct Cleanup
    {
        fs::path directory;
        fs::path root;
        ~Cleanup()
        {
            std::error_code ec;
            if (fs::exists(directory))
                for (const auto& entry : fs::directory_iterator(directory, ec))
                    fs::remove(entry.path(), ec);
            fs::remove(directory, ec);
            fs::remove(root, ec);
        }
    } cleanup{ objectDirectory, root };
    auto repository = CreateObjectRepository(*env);
    // Deliberately invalid object bytes: this tests storage naming even when the later object scan rejects the payload.
    const std::array<uint8_t, 4> payload{ 1, 2, 3, 4 };
    repository->AddObjectFromFile(ObjectGeneration::json, "../escaped", payload.data(), payload.size());
    repository->AddObjectFromFile(ObjectGeneration::json, "../escaped", payload.data(), payload.size());
    repository->AddObjectFromFile(ObjectGeneration::json, "   ", payload.data(), payload.size());
    repository->AddObjectFromFile(ObjectGeneration::json, "normal.name", payload.data(), payload.size());
    EXPECT_FALSE(fs::exists(root / "escaped.parkobj"));
    for (const auto* name : { ".._escaped.parkobj", ".._escaped-02.parkobj", "object_with_invalid_name.parkobj",
                             "normal.name.parkobj" })
    {
        SCOPED_TRACE(name);
        const auto file = objectDirectory / name;
        ASSERT_TRUE(fs::exists(file));
        EXPECT_EQ(File::ReadAllBytes(file.u8string()), (std::vector<uint8_t>{ 1, 2, 3, 4 }));
    }
}
