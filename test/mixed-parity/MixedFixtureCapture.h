// STAGED test-only capture helper. Compile into the frozen fixture executable,
// not the production renderer. Caller supplies independently drawn software
// viewport pixels and raw fixture ABI bytes; never capture a dynamic draw list.
#pragma once
#include <openrct2/core/Json.hpp>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/drawing/PaletteMap.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <stdexcept>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace MixedFixtureCapture
{
    inline void Write(const std::filesystem::path& path,std::span<const std::byte> bytes)
    {
        std::ofstream out(path,std::ios::binary); out.exceptions(std::ios::badbit|std::ios::failbit);
        out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    }
    inline std::string Hex(std::span<const uint8_t> bytes)
    {
        constexpr char digits[]="0123456789abcdef"; std::string result; result.reserve(bytes.size()*2);
        for(uint8_t value:bytes) { result+=digits[value>>4]; result+=digits[value&15]; } return result;
    }
    // Requested images are the union of immutable finite static definitions and
    // whole admitted animation groups/accessory sets, not images chosen per peep.
    // The same resolved IDs must remain pinned across the complete capture.
    inline json_t Assets(const std::set<uint32_t>& requested)
    {
        json_t result=json_t::array();
        for(uint32_t image:requested)
        {
            const auto* g1=GfxGetG1Element(image);
            if(!g1 || g1->width<=0 || g1->height<=0) throw std::runtime_error("Missing mixed fixture asset");
            using OpenRCT2::Drawing::PaletteIndex;
            const size_t size=size_t(g1->width)*g1->height;
            std::vector<PaletteIndex> zero(size,PaletteIndex::transparent),other(size,static_cast<PaletteIndex>(255));
            OpenRCT2::Drawing::RenderTarget target{};
            target.width=g1->width; target.height=g1->height; target.bits=zero.data();
            GfxDrawSpriteSoftware(target,ImageId(image),{-g1->xOffset,-g1->yOffset});
            target.bits=other.data(); GfxDrawSpriteSoftware(target,ImageId(image),{-g1->xOffset,-g1->yOffset});
            size_t coveredZero=0;
            for(size_t i=0;i<size;++i)
                coveredZero+=zero[i]==PaletteIndex::transparent && other[i]==PaletteIndex::transparent;
            if(coveredZero!=0) throw std::runtime_error("Mixed fixture covered-zero image requires another composition path");
            result.push_back({{"image",image},{"width",g1->width},{"height",g1->height},{"xOffset",g1->xOffset},
                {"yOffset",g1->yOffset},{"flags",g1->flags.holder},{"coveredZeroPixels",coveredZero},
                {"decodedIndexedHex",Hex({reinterpret_cast<const uint8_t*>(zero.data()),zero.size()})}});
        }
        return result;
    }
    // Export the exact renderer palette/remap tables from the pinned asset load.
    // PaletteToY uses identity row0 and ordinary colour rows at colour+1. The
    // caller supplies the already established 65536-byte GPU remap layout.
    inline void Begin(const std::filesystem::path& output,json_t manifest,const std::set<uint32_t>& requested,
        std::span<const std::byte> statics,std::span<const std::byte> definitions,
        std::span<const std::byte> descriptors,std::span<const std::byte> facts,
        std::span<const std::byte> remap,std::span<const std::byte> palette)
    {
        if(std::filesystem::exists(output)) throw std::runtime_error("Mixed corpus directory must be fresh");
        if(remap.size()!=65536 || palette.size()!=1024) throw std::runtime_error("Invalid mixed palette tables");
        std::filesystem::create_directories(output);
        manifest["schema"]=1; manifest["assets"]=Assets(requested); manifest["cases"]=json_t::array();
        manifest["oracle"]= "frozen-software-viewport";
        Write(output/"statics.bin",statics); Write(output/"definitions.bin",definitions);
        Write(output/"descriptors.bin",descriptors); Write(output/"facts.bin",facts);
        Write(output/"remap.bin",remap); Write(output/"palette.bin",palette);
        OpenRCT2::Json::WriteToFile((output/"corpus.json").string(),manifest);
    }
    inline void AddCase(const std::filesystem::path& output,std::string_view name,uint32_t rotation,
        const std::array<int32_t,4>& viewport,std::span<const std::byte> rawPeeps,std::span<const std::byte> softwareIndices)
    {
        if(name.empty() || name=="." || name==".." || std::filesystem::path(name)!=std::filesystem::path(name).filename() || rotation>=4
            || viewport[2]<=0 || viewport[3]<=0 || softwareIndices.size()!=size_t(viewport[2])*viewport[3])
            throw std::runtime_error("Bad mixed fixture case");
        std::ifstream input(output/"corpus.json"); json_t manifest; input>>manifest;
        const auto prefix=std::string(name);
        for(const auto& previous:manifest.at("cases"))
            if(previous.at("name")==prefix) throw std::runtime_error("Duplicate mixed fixture case");
        Write(output/(prefix+".peeps.bin"),rawPeeps); Write(output/(prefix+".indexed.bin"),softwareIndices);
        manifest["cases"].push_back({{"name",prefix},{"rotation",rotation},{"target",viewport},
            {"peeps",prefix+".peeps.bin"},{"expected",prefix+".indexed.bin"}});
        OpenRCT2::Json::WriteToFile((output/"corpus.json").string(),manifest);
    }
}
