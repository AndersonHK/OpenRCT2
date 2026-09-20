// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <openrct2/core/Json.hpp>
#include <openrct2-ui/drawing/engines/vulkan/VulkanDiagnosticCapture.h>

namespace UiParityTerrain
{
    inline json_t Describe(const OpenRCT2::Ui::Vulkan::Diagnostic::CaptureResult& capture)
    {
        const auto& u = capture.output.terrainUploads;
        json_t result = {{"schema", 1}, {"viewports", capture.terrainScenes.size()}, {"atlasLease", capture.atlasLease},
            {"uploads", {{"tiles",u.tileBytes},{"materials",u.materialBytes},{"sprites",u.spriteBytes},
                {"statusReadback",u.statusReadbackBytes},{"viewports",u.viewportSubmissions}}}, {"scenes",json_t::array()}};
        const auto& preparation = capture.terrainPreparation;
        result["preparation"] = {{"schema",1},{"frameNumber",capture.coverage.frameNumber},
            {"epoch",preparation[0]},{"materialMapCopies",preparation[1]},
            {"spriteCatalogBuilds",preparation[2]},{"residencyRebinds",preparation[3]}};
        if (capture.terrainPublication)
        {
            const auto& source = *capture.terrainPublication;
            result["source"] = {{"epoch",source.GetEpoch()}, {"bounded",source.HasBoundedTerrainFacts()},
                {"materialRevision",source.GetTerrainMaterials() ? source.GetTerrainMaterials()->revision : 0},
                {"chunkRevisions",json_t::array()}, {"tiles",json_t::array()}};
            for (const auto& chunk : source.GetSurfaceChunks())
            {
                if (!chunk) throw std::runtime_error("Missing named terrain source chunk");
                result["source"]["chunkRevisions"].push_back(chunk->revision);
                for (const auto& record : chunk->records)
                {
                    const auto& t = record.terrain;
                    result["source"]["tiles"].push_back({t.baseZ,t.slope,t.grass,t.surfaceSlot,t.edgeSlot,t.kind});
                }
            }
            result["source"]["materials"] = json_t::array();
            if (source.GetTerrainMaterials())
            {
                const auto& catalog = *source.GetTerrainMaterials();
                for (uint32_t slot = 0; slot < 255; slot++)
                    for (uint32_t kind = 1; kind <= 2; kind++)
                    {
                        const auto& m = kind == 1 ? catalog.surfaces[slot] : catalog.edges[slot];
                        if (m.supported)
                            result["source"]["materials"].push_back({{"slot",slot},{"kind",kind},
                                {"base",m.imageBase},{"count",m.imageCount},{"selectors",m.selectors}});
                    }
            }
        }
        for (const auto& scene : capture.terrainScenes)
        {
            const auto& c = scene.camera;
            json_t item = {{"epoch",scene.snapshot.worldEpoch}, {"materialRevision",scene.snapshot.materials->revision},
                {"spriteRevision",scene.sprites->revision},
                {"camera",{c.x,c.y,c.width,c.height,c.clipX,c.clipY,c.rotation,c.zoom,c.transparent,c.stableSort,c.depthBase}},
                {"chunkRevisions",json_t::array()}, {"tiles",json_t::array()},
                {"materials",json_t::array()}, {"sprites",json_t::array()}};
            for (const auto& chunk : scene.snapshot.chunks)
            {
                item["chunkRevisions"].push_back(chunk->revision);
                for (const auto& t : chunk->records)
                    item["tiles"].push_back({t.baseZ,t.slope,t.grass,t.surfaceMaterial,t.edgeMaterial,t.kind});
            }
            for (const auto& m : scene.snapshot.materials->records)
                item["materials"].push_back({{"base",m.imageBase},{"count",m.imageCount},{"kind",m.kind},
                    {"flags",m.flags},{"selectors",m.selectors.entries}});
            for (const auto& s : scene.sprites->records)
            {
                json_t sprite = {{"image",s.imageIndex},{"original",{s.width,s.height,s.xOffset,s.yOffset}},
                    {"variants",json_t::array()}};
                for (const auto& v : s.variants)
                    sprite["variants"].push_back({v.width,v.height,v.xOffset,v.yOffset,v.asset,v.flags,v.effectiveZoom,v.coordinateShift});
                item["sprites"].push_back(std::move(sprite));
            }
            result["scenes"].push_back(std::move(item));
        }
        return result;
    }
}
