# Shared recorder, resident resources and Vulkan execution. SDL presentation remains in UI.
add_library(libopenrct2renderer STATIC
    "${CMAKE_CURRENT_LIST_DIR}/RenderServiceFactory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu/GpuCommandDrawingContext.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu/TerrainPresentationBridge.h"
    "${CMAKE_CURRENT_LIST_DIR}/gpu/GpuGraphicsLookupTables.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu/GpuTextureCache.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu/GpuTransparencyDepth.cpp")
add_library(OpenRCT2::renderer ALIAS libopenrct2renderer)
target_include_directories(libopenrct2renderer PUBLIC "${CMAKE_CURRENT_LIST_DIR}/..")
target_link_libraries(libopenrct2renderer PUBLIC OpenRCT2::libopenrct2)
set_target_properties(libopenrct2renderer PROPERTIES PREFIX "")
SET_CHECK_CXX_FLAGS(libopenrct2renderer)
ipo_set_target_properties(libopenrct2renderer)

set(OPENRCT2_VULKAN_ENABLED OFF)
if (ENABLE_VULKAN AND NOT EMSCRIPTEN)
    find_package(Vulkan QUIET)
    if (Vulkan_FOUND)
        if (NOT Vulkan_GLSLC_EXECUTABLE)
            find_program(Vulkan_GLSLC_EXECUTABLE NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin")
        endif ()

        set(VULKAN_SHADER_SOURCE_DIR "${ROOT_DIR}/data/shaders/vulkan")
        set(VULKAN_SHADER_SOURCES
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_palette.vert"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_palette.frag"
            "${VULKAN_SHADER_SOURCE_DIR}/rgba_scale.frag"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_line.vert"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_line.frag"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_rect.vert"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_rect.frag"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_sprite.vert"
            "${VULKAN_SHADER_SOURCE_DIR}/balloon.vert"
            "${VULKAN_SHADER_SOURCE_DIR}/balloon_order.comp"
            "${VULKAN_SHADER_SOURCE_DIR}/world_surface.vert"
            "${VULKAN_SHADER_SOURCE_DIR}/world_surface.frag"
            "${VULKAN_SHADER_SOURCE_DIR}/world_filter_collect.frag"
            "${VULKAN_SHADER_SOURCE_DIR}/world_filter_resolve.comp"
            "${VULKAN_SHADER_SOURCE_DIR}/world_surface_compact.comp"
            "${VULKAN_SHADER_SOURCE_DIR}/world_parent_columns.comp"
            "${VULKAN_SHADER_SOURCE_DIR}/terrain_retained_emit.comp"
            "${VULKAN_SHADER_SOURCE_DIR}/terrain_columns.comp"
            "${VULKAN_SHADER_SOURCE_DIR}/peep_fields.comp"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_transparent_rect.vert"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_transparent_rect.frag"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_transparency_compose.vert"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_transparency_compose.frag"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_weather.vert"
            "${VULKAN_SHADER_SOURCE_DIR}/indexed_weather.frag"
            "${VULKAN_SHADER_SOURCE_DIR}/lightfx_accumulate.comp"
            "${VULKAN_SHADER_SOURCE_DIR}/image_alias.comp")
        if (Vulkan_GLSLC_EXECUTABLE)
            set(OPENRCT2_VULKAN_SHADER_MODE "compile")
            set(OPENRCT2_VULKAN_ENABLED ON)
            message(STATUS "Vulkan renderer foundation enabled with ${Vulkan_GLSLC_EXECUTABLE}")
        elseif (EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_palette.vert.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_palette.frag.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/rgba_scale.frag.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_line.vert.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_line.frag.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_rect.vert.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_rect.frag.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_sprite.vert.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/balloon.vert.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/balloon_order.comp.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/world_surface.vert.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/world_surface.frag.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/world_filter_collect.frag.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/world_filter_resolve.comp.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/world_surface_compact.comp.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/world_parent_columns.comp.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/terrain_retained_emit.comp.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/terrain_columns.comp.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/peep_fields.comp.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_transparent_rect.vert.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_transparent_rect.frag.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_transparency_compose.vert.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_transparency_compose.frag.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_weather.vert.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/indexed_weather.frag.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/lightfx_accumulate.comp.spv"
                AND EXISTS "${VULKAN_SHADER_SOURCE_DIR}/image_alias.comp.spv")
            set(OPENRCT2_VULKAN_SHADER_MODE "precompiled")
            set(OPENRCT2_VULKAN_ENABLED ON)
            message(STATUS "Vulkan renderer foundation enabled with checked-in SPIR-V shaders")
        else ()
            message(STATUS "Vulkan SDK was found without glslc or the complete checked-in SPIR-V set; Vulkan renderer foundation disabled")
        endif ()
    else ()
        message(STATUS "Vulkan SDK or MoltenVK not found; Vulkan renderer foundation disabled")
    endif ()
endif ()

if (OPENRCT2_VULKAN_ENABLED)
    target_sources(libopenrct2renderer PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanFrameExecutor.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanRenderService.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanImageAliasPipeline.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanBackend.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanDevice.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanDeviceContext.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanSubmissionSlots.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanResources.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanShader.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanLinePipeline.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanRectPipeline.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanBalloonPipeline.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanWorldSurfacePipeline.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanWorldFilterCompositor.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanTerrainEmissionPipeline.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanTerrainDrawPipeline.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanPeepFieldPipeline.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanTransparencyPipeline.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanWeatherPipeline.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanLightFxPipeline.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/VulkanPalettePipeline.cpp"
    )
    target_link_libraries(libopenrct2renderer PUBLIC Vulkan::Vulkan)
    target_compile_definitions(libopenrct2renderer PUBLIC ENABLE_VULKAN)
    target_compile_definitions(libopenrct2 PUBLIC ENABLE_VULKAN)
endif ()

if (OPENRCT2_VULKAN_ENABLED)
    if (MACOS_BUNDLE AND NOT DISABLE_GUI)
        set(VULKAN_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/OpenRCT2.app/Contents/Resources/shaders/vulkan")
    else ()
        set(VULKAN_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/data/shaders/vulkan")
    endif ()

    set(VULKAN_SHADER_OUTPUTS)
    foreach (VULKAN_SHADER_SOURCE IN LISTS VULKAN_SHADER_SOURCES)
        get_filename_component(VULKAN_SHADER_NAME "${VULKAN_SHADER_SOURCE}" NAME)
        set(VULKAN_SHADER_OUTPUT "${VULKAN_SHADER_OUTPUT_DIR}/${VULKAN_SHADER_NAME}.spv")
        list(APPEND VULKAN_SHADER_OUTPUTS "${VULKAN_SHADER_OUTPUT}")
        set(VULKAN_SHADER_INCLUDES)
        if (VULKAN_SHADER_NAME STREQUAL "terrain_retained_emit.comp")
            list(APPEND VULKAN_SHADER_INCLUDES "${VULKAN_SHADER_SOURCE_DIR}/terrain_surface_rules.glsl")
        elseif (VULKAN_SHADER_NAME STREQUAL "world_surface_compact.comp")
            list(APPEND VULKAN_SHADER_INCLUDES "${VULKAN_SHADER_SOURCE_DIR}/world_surface_rules.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_parent_capture.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_path_rules.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_path_order.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_prop_rules.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_prop_emit.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_track_rules.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_track_emit.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_track_station.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_track_photo.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_selection_emit.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_surface_tunnel.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_tunnel_rules.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_flat_ride_rules.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_flat_ride_animation.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_flat_ride_emit.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_static_tower_maze_rules.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_maze_order.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_entrance_rules.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/world_entrance_emit.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/terrain_sprite_geometry.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/terrain_surface_rules.glsl")
        elseif (VULKAN_SHADER_NAME STREQUAL "world_surface.vert" OR VULKAN_SHADER_NAME STREQUAL "world_parent_columns.comp")
            list(APPEND VULKAN_SHADER_INCLUDES "${VULKAN_SHADER_SOURCE_DIR}/terrain_sprite_geometry.glsl")
        elseif (VULKAN_SHADER_NAME STREQUAL "terrain_columns.comp")
            list(APPEND VULKAN_SHADER_INCLUDES "${VULKAN_SHADER_SOURCE_DIR}/terrain_sprite_geometry.glsl"
                "${VULKAN_SHADER_SOURCE_DIR}/peep_rules.glsl")
        endif ()
        if (OPENRCT2_VULKAN_SHADER_MODE STREQUAL "compile")
            add_custom_command(
                OUTPUT "${VULKAN_SHADER_OUTPUT}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_OUTPUT_DIR}"
                COMMAND "${Vulkan_GLSLC_EXECUTABLE}" -I "${VULKAN_SHADER_SOURCE_DIR}" "${VULKAN_SHADER_SOURCE}" -o "${VULKAN_SHADER_OUTPUT}"
                DEPENDS "${VULKAN_SHADER_SOURCE}" ${VULKAN_SHADER_INCLUDES}
                VERBATIM)
        else ()
            add_custom_command(
                OUTPUT "${VULKAN_SHADER_OUTPUT}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_OUTPUT_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VULKAN_SHADER_SOURCE}.spv" "${VULKAN_SHADER_OUTPUT}"
                DEPENDS "${VULKAN_SHADER_SOURCE}.spv"
                VERBATIM)
        endif ()
    endforeach ()
    add_custom_target(openrct2-vulkan-shaders DEPENDS ${VULKAN_SHADER_OUTPUTS})
    add_dependencies(libopenrct2renderer openrct2-vulkan-shaders)
endif ()
