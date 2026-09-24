cmake_minimum_required(VERSION 3.20)
include(GoogleTest)

find_package(GTest REQUIRED)

file(CREATE_LINK "${CMAKE_CURRENT_LIST_DIR}/testdata" "${CMAKE_BINARY_DIR}/testdata" SYMBOLIC)
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_DATADIR}/openrct2\" \"${CMAKE_BINARY_DIR}/data\")")

set(test_files
    "${CMAKE_CURRENT_SOURCE_DIR}/ObjectDownloaderTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/AssertHelpers.hpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/BitSetTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/CircularBuffer.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/CLITests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/ClearSceneryTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/CryptTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/Endianness.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/EnumMapTest.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/FormattingTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/GpuFoundationTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanBackendContractTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanBalloonPipelineTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanOffscreenDeviceTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanOffscreenRenderTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/PublicationSnapshotParityTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/RetainedBalloonSceneTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/TerrainSurfaceRulesTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/RetainedPeepStateTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/NativePeepPublicationTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/PeepRulesTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMixedFixtureTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/../mixed-parity/VulkanMixedFixturePipeline.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/PaintOrderingTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/TerrainPresentationBridgeTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/TerrainSurfaceEmissionTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanRetainedTerrainEmissionTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanRetainedTerrainDrawTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanNativePeepSceneTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanWorldSurfaceLayerTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanWorldPathLayerTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanWorldObjectLayerTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/WorldTrackRulesTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/WorldFlatRideRulesTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanWorldPathArtTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/WorldPathPublicationTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/WorldSelectionTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/SelectedVehicleSnapshotTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/WorldPathRulesTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/WorldPathOrderTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/VulkanTerrainCompletionTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/SpriteAssetDecoderTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/AssetMetadataCorpusParityTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/ImageImporterTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/IniReaderTest.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/ConfigTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/ParkPreviewTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/TrackDesignPreviewTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/IniWriterTest.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/JobPoolTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/RenderServiceTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/ScreenshotTilingTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/ViewportGenerationTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/MapPathTopologyTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/MapTopologyTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/LanguagePackTest.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/LocalisationTest.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/MoneyTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/MultiLaunch.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/NetworkTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/Pathfinding.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/Platform.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/PlayTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/ReplayTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/RideRatings.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/RideVehicleCreation.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/EntityImportTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/S6ImportExportTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/SawyerCodingTest.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/ScenarioPatcherTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/ScriptingTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/SpatialAudioTests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/StringTest.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/TestData.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/TestData.h"
   "${CMAKE_CURRENT_SOURCE_DIR}/tests.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/TileElements.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/TileElementsView.cpp"
   "${CMAKE_CURRENT_SOURCE_DIR}/WidgetStateTests.cpp")

# Windowed parity remains selected by default for GUI builds. A GUI-disabled
# build can explicitly opt in, but must then provide SDL; the shared renderer never does.
set(OPENRCT2_VULKAN_WINDOW_TESTS_DEFAULT ON)
if (DISABLE_GUI)
    set(OPENRCT2_VULKAN_WINDOW_TESTS_DEFAULT OFF)
endif ()
option(WITH_VULKAN_WINDOW_TESTS "Build SDL windowed Vulkan parity/lifecycle fixtures" ${OPENRCT2_VULKAN_WINDOW_TESTS_DEFAULT})
if (WITH_VULKAN_WINDOW_TESTS)
    list(APPEND test_files
        "${CMAKE_CURRENT_SOURCE_DIR}/VulkanRuntimeIntegrationTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/VulkanParityTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/VulkanScalingParityTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/VulkanSceneParityTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/VulkanTtfParityTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/VulkanSpatialLightParityTests.cpp"
    )
else ()
    message(STATUS "WINDOWED_PARITY_COVERAGE=disabled: SDL window fixtures are not compiled; this build cannot qualify full windowed parity")
endif ()

add_executable(OpenRCT2Tests ${test_files})
add_executable(OpenRCT2::OpenRCT2Tests ALIAS OpenRCT2Tests)

target_link_libraries(OpenRCT2Tests GTest::gtest GTest::gtest_main libopenrct2 OpenRCT2::renderer)
target_include_directories(OpenRCT2Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
# Core spatial math remains tested without linking SDL audio implementations.
if (DISABLE_GUI OR NOT WITH_VULKAN_WINDOW_TESTS)
    target_compile_definitions(OpenRCT2Tests PRIVATE OPENRCT2_TEST_NO_UI_AUDIO)
    message(STATUS "UI_AUDIO_COVERAGE=disabled: four SDL audio implementation tests are not compiled; core spatial audio tests remain enabled")
endif ()
if (OPENRCT2_VULKAN_ENABLED)
    # Backend symbols/SDK usage come from the single shared renderer owner.
    add_dependencies(OpenRCT2Tests openrct2-vulkan-shaders)
    if (WITH_VULKAN_WINDOW_TESTS)
        if (MSVC)
            find_package(SDL2 REQUIRED)
        elseif (NOT WIN32)
            PKG_CHECK_MODULES(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        else ()
            find_path(SDL2_INCLUDE_DIRS SDL.h PATH_SUFFIXES SDL2 REQUIRED)
            if (CMAKE_BUILD_TYPE STREQUAL "Debug")
                find_library(SDL2_LDFLAGS sdl2d REQUIRED)
            else ()
                find_library(SDL2_LDFLAGS sdl2 REQUIRED)
            endif ()
        endif ()
        target_sources(OpenRCT2Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/openrct2-ui/drawing/engines/vulkan/VulkanPlatform.cpp")
        target_include_directories(OpenRCT2Tests SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
        if (MSVC)
            target_link_libraries(OpenRCT2Tests SDL2::SDL2-static)
        elseif (NOT WIN32)
            target_link_libraries(OpenRCT2Tests PkgConfig::SDL2)
        else ()
            target_link_libraries(OpenRCT2Tests ${SDL2_LDFLAGS})
        endif ()
        message(STATUS "WINDOWED_PARITY_COVERAGE=enabled: SDL windowed Vulkan fixtures are compiled")
    endif ()
endif ()
set_target_properties(OpenRCT2Tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
gtest_discover_tests(OpenRCT2Tests
    TEST_FILTER "*-ExperimentalVulkanMixedFixtureTest.*"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST)
