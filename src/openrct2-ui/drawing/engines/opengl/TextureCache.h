/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../gpu/GpuAtlas.h"
#include "OpenGLAPI.h"

#include <array>
#include <openrct2/SpriteIds.h>
#include <unordered_map>
#include <vector>

namespace OpenRCT2::Drawing
{
    enum class FilterPaletteID : int32_t;
    struct PaletteMap;
    struct RenderTarget;
} // namespace OpenRCT2::Drawing

namespace OpenRCT2::Ui
{
    struct GlyphId
    {
        ImageIndex Image;
        uint64_t Palette;

        struct Hash
        {
            size_t operator()(const GlyphId& k) const
            {
                size_t hash = k.Image * 7;
                hash += (k.Palette & 0xFFFFFFFFuL) * 13;
                hash += (k.Palette >> 32uL) * 23;
                return hash;
            }
        };

        struct Equal
        {
            bool operator()(const GlyphId& lhs, const GlyphId& rhs) const
            {
                return lhs.Image == rhs.Image && lhs.Palette == rhs.Palette;
            }
        };
    };

    constexpr int32_t kTextureCacheMaxAtlasSize = Gpu::kAtlasDimension;
    constexpr int32_t kTextureCacheSmallestSlot = Gpu::kSmallestAtlasSlot;
    using BasicTextureInfo = Gpu::TextureBinding;
    using AtlasTextureInfo = Gpu::TextureLocation;
    using Atlas = Gpu::AtlasPage;

    class TextureCache final
    {
    private:
        bool _initialized = false;

        GLuint _atlasesTexture = 0;
        GLint _atlasesTextureDimensions = 0;
        GLuint _atlasesTextureCapacity = 0;
        GLuint _atlasesTextureIndices = 0;
        GLint _atlasesTextureIndicesLimit = 0;
        std::vector<Atlas> _atlases;
        std::unordered_map<GlyphId, AtlasTextureInfo, GlyphId::Hash, GlyphId::Equal> _glyphTextureMap;
        std::vector<AtlasTextureInfo> _textureCache;
        std::array<uint32_t, SPR_IMAGE_LIST_END> _indexMap;

        GLuint _paletteTexture = 0;
        GLuint _blendPaletteTexture = 0;

    public:
        TextureCache();
        ~TextureCache();
        void InvalidateImage(ImageIndex image);
        BasicTextureInfo GetOrLoadImageTexture(ImageId imageId);
        BasicTextureInfo GetOrLoadGlyphTexture(ImageId imageId, const Drawing::PaletteMap& paletteMap);
        BasicTextureInfo GetOrLoadBitmapTexture(ImageIndex image, const void* pixels, size_t width, size_t height);

        GLuint GetAtlasesTexture();
        GLuint GetPaletteTexture();
        GLuint GetBlendPaletteTexture();
        static GLint PaletteToY(Drawing::FilterPaletteID palette);

    private:
        void CreateTextures();
        void GeneratePaletteTexture();
        void EnlargeAtlasesTexture(GLuint newEntries);
        AtlasTextureInfo LoadImageTexture(ImageId image);
        AtlasTextureInfo LoadGlyphTexture(ImageId image, const Drawing::PaletteMap& paletteMap);
        AtlasTextureInfo AllocateImage(int32_t imageWidth, int32_t imageHeight);
        AtlasTextureInfo LoadBitmapTexture(ImageIndex image, const void* pixels, size_t width, size_t height);
        static Drawing::RenderTarget GetImageAsRT(ImageId imageId);
        static Drawing::RenderTarget GetGlyphAsRT(ImageId imageId, const Drawing::PaletteMap& paletteMap);
        void FreeTextures();

        static Drawing::RenderTarget CreateRT(int32_t width, int32_t height);
        static void DeleteRT(Drawing::RenderTarget rt);
    };
} // namespace OpenRCT2::Ui
