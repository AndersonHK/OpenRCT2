/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2/config/Config.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
    class ConfigMigrationTests : public testing::Test
    {
    protected:
        std::filesystem::path _directory;
        bool _savedOriginal{};

        void SetUp() override
        {
            _directory = std::filesystem::temp_directory_path()
                / ("openrct2-config-migration-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            ASSERT_TRUE(std::filesystem::create_directory(_directory));
            _savedOriginal = OpenRCT2::Config::SaveToPath((_directory / "original.ini").u8string());
            ASSERT_TRUE(_savedOriginal);
            ASSERT_TRUE(OpenRCT2::Config::SetDefaults());
        }

        void TearDown() override
        {
            if (_savedOriginal)
                EXPECT_TRUE(OpenRCT2::Config::OpenFromPath((_directory / "original.ini").u8string()));
            std::error_code error;
            for (const auto* name : { "original.ini", "input.ini", "output.ini" })
                std::filesystem::remove(_directory / name, error);
            std::filesystem::remove(_directory, error);
        }

        void WriteInput(const std::string& contents)
        {
            std::ofstream output(_directory / "input.ini", std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::failbit | std::ios::badbit);
            output << contents;
        }

        std::string ReadOutput()
        {
            std::ifstream input(_directory / "output.ini", std::ios::binary);
            input.exceptions(std::ios::badbit);
            return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        }
    };
}

TEST_F(ConfigMigrationTests, LegacyRendererKeysAreDiscardedWithoutChangingLightingPreferences)
{
    using namespace OpenRCT2;
    for (const bool enabled : { false, true })
    {
        SCOPED_TRACE(enabled);
        const std::string preferences = std::string("enable_light_fx = ") + (enabled ? "true" : "false")
            + "\nenable_light_fx_for_vehicles = " + (enabled ? "false" : "true")
            + "\nenable_hdr10_output = " + (enabled ? "true" : "false") + "\n";
        WriteInput("[general]\n" + preferences);
        ASSERT_TRUE(Config::OpenFromPath((_directory / "input.ini").u8string()));
        ASSERT_TRUE(Config::SaveToPath((_directory / "output.ini").u8string()));
        const auto withoutRendererKey = ReadOutput();

        for (const auto* legacyRenderer : { "SOFTWARE_HWD", "OPENGL", "VULKAN", "UNKNOWN_RENDERER" })
        {
            SCOPED_TRACE(legacyRenderer);
            WriteInput(std::string("[general]\ndrawing_engine = ") + legacyRenderer + "\n" + preferences);
            ASSERT_TRUE(Config::OpenFromPath((_directory / "input.ini").u8string()));
            EXPECT_EQ(Config::Get().general.enableLightFx, enabled);
            EXPECT_EQ(Config::Get().general.enableLightFxForVehicles, !enabled);
            EXPECT_EQ(Config::Get().general.enableHdr10Output, enabled);
            ASSERT_TRUE(Config::SaveToPath((_directory / "output.ini").u8string()));
            const auto migrated = ReadOutput();
            EXPECT_EQ(migrated.find("drawing_engine"), std::string::npos);
            EXPECT_EQ(migrated, withoutRendererKey);

            // Read the saved file with opposing in-memory values so persistence is verified as well.
            Config::Get().general.enableLightFx = !enabled;
            Config::Get().general.enableLightFxForVehicles = enabled;
            Config::Get().general.enableHdr10Output = !enabled;
            ASSERT_TRUE(Config::OpenFromPath((_directory / "output.ini").u8string()));
            EXPECT_EQ(Config::Get().general.enableLightFx, enabled);
            EXPECT_EQ(Config::Get().general.enableLightFxForVehicles, !enabled);
            EXPECT_EQ(Config::Get().general.enableHdr10Output, enabled);
        }
    }
}
