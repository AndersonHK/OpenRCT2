"""Generate, but do not configure/build, an isolated SDL-free renderer compile check.

Uses the real renderer CMakeLists.txt, its twenty-two translation units and shader
target. A tiny core adapter publishes only source headers and the receipt-pinned
core library; it supplies no SDL headers/libraries. This qualifies module/header
compilation, not core dependency packaging, renderer execution or pixel parity.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil


SPEC = importlib.util.spec_from_file_location(
    "no_window_builder", Path(__file__).with_name("build-no-window-tests.py"))
HELPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPER)
BUILD = HELPER.BUILD


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-window-build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, required=True)
    parser.add_argument("--allow-failed-test-link", action="store_true",
                        help="Use independently verified libraries from a stable failed test-link build; never treats tests as passed")
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if output.exists() or workspace not in output.parents:
        raise SystemExit("--output must be a new directory inside the workspace")
    receipt_path = args.no_window_build.resolve(strict=True)
    if receipt_path.is_dir():
        receipt_path /= "receipt.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    failed_link = (args.allow_failed_test_link and receipt["status"] == "fail"
                   and receipt.get("missingArtifacts") == ["bin/tests-no-window.exe"]
                   and len(receipt.get("stageResults", [])) == 1
                   and receipt["stageResults"][0]["name"] == "no-window-tests"
                   and receipt["stageResults"][0]["exitCode"] != 0)
    if (receipt["status"] not in ("pass", "prepared") and not failed_link) or any(receipt.get(key) for key in (
            "sourceChangesDuringBuild", "testChangesDuringBuild", "dependencyChangesDuringBuild", "sdkChangesDuringBuild")):
        raise SystemExit("A successful or prepared, stable no-window build is required")
    if BUILD.sha256(receipt_path.parent / "build.log") != receipt["buildLogSha256"]:
        raise SystemExit("No-window build log changed")
    library_receipt_path, libraries = HELPER.load_receipt(Path(receipt["libraryReceipt"]))
    if BUILD.sha256(library_receipt_path) != receipt["libraryReceiptSha256"]:
        raise SystemExit("Library input receipt changed")
    if set(receipt["reusedLibraries"]) != {"core", "renderer"}:
        raise SystemExit("Both libraries require independent successful reuse verification")
    for relative in ("bin/libopenrct2.lib", "bin/libopenrct2renderer.lib"):
        if (BUILD.sha256(receipt_path.parent / relative) != receipt["artifactSha256"][relative]
                or receipt["artifactSha256"][relative] != libraries["artifactSha256"][relative]):
            raise SystemExit("Reusable library differs from successful input receipt: " + relative)
    source = Path(receipt["sourceRoot"])
    current = BUILD.source_manifest(source, source)
    if BUILD.compile_inputs(current, True) != BUILD.compile_inputs(receipt["sourceSha256"], True):
        raise SystemExit("Source differs from the verified no-window build")
    core = receipt_path.parent / "bin/libopenrct2.lib"
    if BUILD.sha256(core) != receipt["artifactSha256"]["bin/libopenrct2.lib"]:
        raise SystemExit("Receipt-pinned core library changed")
    cmake = args.cmake.resolve(strict=True)
    output.mkdir(parents=True)
    shutil.copy2(core, output / "libopenrct2-pinned.lib")
    # Keep the actual module CMake intact: it adds PUBLIC compile definitions to
    # libopenrct2, so that target must be a build target rather than IMPORTED.
    (output / "core-anchor.cpp").write_text("void OpenRCT2RendererCompileCheckCoreAnchor() {}\n", encoding="utf-8")
    (output / "consumer.cpp").write_text("""#include <openrct2-renderer/gpu/GpuCommandDrawingContext.h>
#include <openrct2-renderer/gpu/GpuTextureCache.h>
#include <openrct2-renderer/vulkan/VulkanBackend.h>
#include <type_traits>
static_assert(std::is_abstract_v<OpenRCT2::Ui::Vulkan::PresentationHost>);
static_assert(std::is_base_of_v<OpenRCT2::Ui::Gpu::Backend, OpenRCT2::Ui::Vulkan::Backend>);
int main() { return 0; }
""", encoding="utf-8")
    (output / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.21)
project(OpenRCT2RendererWithoutSDL LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(ROOT_DIR "@SOURCE@")
set(ENABLE_VULKAN ON)
set(DISABLE_GUI ON)
set(MACOS_BUNDLE OFF)
function(SET_CHECK_CXX_FLAGS target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /permissive- /Zc:externConstexpr /EHsc /Zc:char8_t-)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endfunction()
function(ipo_set_target_properties target)
endfunction()
# Header/link contract adapter, not a substitute core implementation. No SDL or
# other pinned dependency include/library directory is added to this project.
add_library(libopenrct2 STATIC core-anchor.cpp)
add_library(OpenRCT2::libopenrct2 ALIAS libopenrct2)
target_include_directories(libopenrct2 PUBLIC "${ROOT_DIR}/src")
target_link_libraries(libopenrct2 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/libopenrct2-pinned.lib")
add_subdirectory("${ROOT_DIR}/src/openrct2-renderer" renderer)
if(NOT TARGET openrct2-vulkan-shaders)
    message(FATAL_ERROR "Actual production Vulkan renderer/shader targets must be enabled")
endif()
get_target_property(renderer_sources libopenrct2renderer SOURCES)
set(renderer_translation_units ${renderer_sources})
list(FILTER renderer_translation_units INCLUDE REGEX "[.]cpp$")
list(LENGTH renderer_translation_units source_count)
if(NOT source_count EQUAL 23)
    message(FATAL_ERROR "Review renderer source ownership change; expected all 23 translation units")
endif()
add_executable(renderer-header-consumer consumer.cpp)
SET_CHECK_CXX_FLAGS(renderer-header-consumer)
target_link_libraries(renderer-header-consumer PRIVATE OpenRCT2::renderer)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/renderer-inputs-$<CONFIG>.txt"
     CONTENT "includes=$<TARGET_PROPERTY:libopenrct2renderer,INCLUDE_DIRECTORIES>\nlinks=$<TARGET_PROPERTY:libopenrct2renderer,LINK_LIBRARIES>\nsources=${renderer_sources}\n")
message(STATUS "WINDOWED_PARITY_COVERAGE=disabled; SDL-free renderer compile/header lane only")
""".replace("@SOURCE@", source.as_posix()), encoding="utf-8")
    toolset = receipt["toolchain"]["properties"]["VCToolsVersion"].rstrip("/\\")
    commands = [
        [str(cmake), "-S", str(output), "-B", str(output / "build"), "-G", "Visual Studio 17 2022",
         "-A", "x64", "-T", "host=x64,version=" + toolset],
        [str(cmake), "--build", str(output / "build"), "--config", "Release", "--target", "renderer-header-consumer",
         "--", "/m:1", "/nr:false"]]
    manifest = {
        "schema": 1, "status": "prepared-not-built", "commands": commands,
        "sourceRoot": str(source), "sourceSha256": current,
        "sourceReceipt": str(receipt_path), "sourceReceiptSha256": BUILD.sha256(receipt_path),
        "sourceTestBuildStatus": receipt["status"], "failedTestLinkExplicitlyAllowed": failed_link,
        "cmake": str(cmake), "cmakeSha256": BUILD.sha256(cmake),
        "sdkSha256": HELPER.sdk_manifest({key.upper(): value for key, value in os.environ.items()}),
        "generatedSha256": {path.name: BUILD.sha256(path) for path in sorted(output.iterdir()) if path.is_file()},
        "scope": "Real production renderer CMake target, all 23 TUs and 21 shaders, plus declaration-only consumer. No SDL include/link metadata supplied; core is a receipt-pinned library behind a header adapter.",
        "limitations": ["No GPU device, surface, renderer call or screenshot is executed",
                        "Does not prove core dependency packaging without SDL",
                        "Does not qualify full GUI-disabled application build or windowed parity"],
    }
    (output / "prepare-receipt.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": manifest["status"], "commands": commands}, indent=2))


if __name__ == "__main__":
    main()
