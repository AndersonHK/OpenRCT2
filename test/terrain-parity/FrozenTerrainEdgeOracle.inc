// GPL-3.0; byte-preserved function/table fragments from frozen reference
// 9a092745f38a714cf33629aaa005d078d473e48b. See FrozenTerrainEdgeOracle.json.
// Included inside the test-only FrozenTerrainOracle namespace after core headers.
static constexpr uint8_t Byte97B444[] = {
    0, 2, 1, 3, 8, 10, 9, 11, 4, 6, 5, 7, 12, 14, 13, 15, 0, 0, 0, 0, 0, 0, 0, 17, 0, 0, 0, 16, 0, 18, 15, 0,
};

// rct2: 0x97B464, 0x97B474, 0x97B484, 0x97B494
static constexpr CoordsXY kNeighbouringTileCoordOffsets[4][kNumOrthogonalDirections] = {
    {
        { 32, 0 },
        { 0, 32 },
        { -32, 0 },
        { 0, -32 },
    },
    {
        { 0, 32 },
        { -32, 0 },
        { 0, -32 },
        { 32, 0 },
    },
    {
        { 0, -32 },
        { 32, 0 },
        { 0, 32 },
        { -32, 0 },
    },
    {
        { -32, 0 },
        { 0, -32 },
        { 32, 0 },
        { 0, 32 },
    },
};

struct TunnelDescriptor
{
    uint8_t height;
    uint8_t boundBoxLength;
    int16_t boundBoxZOffset;
    int8_t lowerEdgeBoundingBoxZ;
    TunnelType lowClearanceAlternative;
    uint8_t imageOffset;
};
static constexpr TunnelDescriptor kTunnels[] = {
    { 2, 2, 0,   15, TunnelType::standardFlat,                    36 }, // TunnelType::standardFlat
    { 3, 3, 0,   15, TunnelType::standardFlat,                    40 }, // TunnelType::standardSlopeStart
    { 3, 5, -32,  4, TunnelType::standardFlat,                    44 }, // TunnelType::standardSlopeEnd
    { 3, 3, 0,   15, TunnelType::invertedFlat,                    48 }, // TunnelType::invertedFlat
    { 4, 4, 0,   15, TunnelType::invertedFlat,                    52 }, // TunnelType::invertedSlopeStart
    { 4, 7, -48,  4, TunnelType::invertedFlat,                    56 }, // TunnelType::invertedSlopeEnd
    { 2, 2, 0,   15, TunnelType::squareFlat,                      60 }, // TunnelType::squareFlat
    { 3, 3, 0,   15, TunnelType::squareFlat,                      64 }, // TunnelType::squareSlopeStart
    { 3, 5, -32,  4, TunnelType::squareFlat,                      68 }, // TunnelType::squareSlopeEnd
    { 3, 3, 0,   15, TunnelType::squareFlat,                      72 }, // TunnelType::invertedSquare
    { 2, 3, -16, 15, TunnelType::pathAndMiniGolf,                 76 }, // TunnelType::pathAndMiniGolf
    { 2, 3, -16, 15, TunnelType::path11,                          80 }, // TunnelType::path11
    { 2, 3, -16,  4, TunnelType::standardFlatTo25Deg,             36 }, // TunnelType::standardFlatTo25Deg
    { 3, 4, -16,  4, TunnelType::invertedFlatTo25Deg,             48 }, // TunnelType::invertedFlatTo25Deg
    { 2, 3, -16,  4, TunnelType::squareFlatTo25Deg,               60 }, // TunnelType::squareFlatTo25Deg
    { 3, 4, -16,  4, TunnelType::squareFlatTo25Deg,               72 }, // TunnelType::invertedSquareFlatTo25Deg
    { 2, 2, 0,   15, TunnelType::doorClosed,                      84 }, // TunnelType::doorClosed
    { 2, 2, 0,   15, TunnelType::doorOpeningOutward,              88 }, // TunnelType::doorOpeningOutward
    { 2, 2, 0,   15, TunnelType::doorOpenOutward,                 92 }, // TunnelType::doorOpenOutward
    { 2, 2, 0,   15, TunnelType::doorOpeningInward,               96 }, // TunnelType::doorOpeningInward
    { 2, 2, 0,   15, TunnelType::doorOpenInward,                 100 }, // TunnelType::doorOpenInward
    { 2, 3, -16,  4, TunnelType::doorClosedFlatToDown25,          84 }, // TunnelType::doorClosedFlatToDown25
    { 2, 3, -16,  4, TunnelType::doorOpeningOutwardFlatToDown25,  88 }, // TunnelType::doorOpeningOutwardFlatToDown25
    { 2, 3, -16,  4, TunnelType::doorOpenOutwardFlatToDown25,     92 }, // TunnelType::doorOpenOutwardFlatToDown25
    { 2, 3, -16,  4, TunnelType::doorOpeningInwardFlatToDown25,   96 }, // TunnelType::doorOpeningInwardFlatToDown25
    { 2, 3, -16,  4, TunnelType::doorOpenInwardFlatToDown25,     100 }, // TunnelType::doorOpenInwardFlatToDown25
};
static_assert(std::size(kTunnels) == kTunnelTypeCount);

struct TileDescriptor
{
    TileCoordsXY tile_coords;
    const TileElement* tile_element;
    const TerrainSurfaceObject* surfaceObject;
    uint8_t slope;
    SlopeRelativeCornerHeights corner_heights;
};

    static constexpr std::array<SlopeRelativeCornerHeights, kTileSlopeMask + 1> kSlopeRelativeCornerHeights = {{
        { 0, 0, 0, 0 },
        { 0, 0, 1, 0 },
        { 0, 0, 0, 1 },
        { 0, 0, 1, 1 },
        { 1, 0, 0, 0 },
        { 1, 0, 1, 0 },
        { 1, 0, 0, 1 },
        { 1, 0, 1, 1 },
        { 0, 1, 0, 0 },
        { 0, 1, 1, 0 },
        { 0, 1, 0, 1 },
        { 0, 1, 1, 1 },
        { 1, 1, 0, 0 },
        { 1, 1, 1, 0 },
        { 1, 1, 0, 1 },
        { 1, 1, 1, 1 },
        { 0, 0, 0, 0 },
        { 0, 0, 1, 0 },
        { 0, 0, 0, 1 },
        { 0, 0, 1, 1 },
        { 1, 0, 0, 0 },
        { 1, 0, 1, 0 },
        { 1, 0, 0, 1 },
        { 1, 0, 1, 2 },
        { 0, 1, 0, 0 },
        { 0, 1, 1, 0 },
        { 0, 1, 0, 1 },
        { 0, 1, 2, 1 },
        { 1, 1, 0, 0 },
        { 1, 2, 1, 0 },
        { 2, 1, 0, 1 },
        { 1, 1, 1, 1 },
    }};
    // clang-format on

    SlopeRelativeCornerHeights GetSlopeRelativeCornerHeights(const uint8_t slope)
    {
        return kSlopeRelativeCornerHeights[slope & kTileSlopeMask];
    }

bool PaintAttachToPreviousPS(PaintSession& session, const ImageId image_id, int32_t x, int32_t y)
{
    auto* masterPs = session.LastPS;
    if (masterPs == nullptr)
    {
        return false;
    }

    auto* ps = session.AllocateAttachedPaintEntry();
    if (ps == nullptr)
    {
        return false;
    }

    ps->image_id = image_id;
    ps->RelativePos = { x, y };
    ps->IsMasked = false;

    AttachedPaintStruct* oldFirstAttached = masterPs->Attached;
    masterPs->Attached = ps;
    ps->NextEntry = oldFirstAttached;

    return true;
}

#define PaintAddImageAsParent CaptureParent
#define PaintAttachToPreviousPS CaptureAttached
static ImageId GetEdgeImageWithOffset(const TerrainEdgeObject* edgeObject, uint32_t offset)
{
    ImageId result;
    if (edgeObject != nullptr)
    {
        result = ImageId(edgeObject->BaseImageId + offset);
    }
    return result;
}

static ImageId GetEdgeImage(const TerrainEdgeObject* edgeObject, uint8_t type)
{
    static constexpr uint32_t offsets[] = {
        0,
        20,
        10,
        30,
    };

    ImageId result;
    if (type < std::size(offsets))
    {
        result = GetEdgeImageWithOffset(edgeObject, offsets[type]);
    }
    return result;
}

static ImageId GetTunnelImage(const TerrainEdgeObject* edgeObject, TunnelType type, edge_t edge)
{
    bool hasDoors = false;
    if (edgeObject != nullptr)
    {
        hasDoors = edgeObject->HasDoors && !edgeObject->UsesFallbackImages();
    }

    if (!hasDoors && EnumValue(type) >= kRegularTunnelTypeCount)
        type = TunnelType::standardFlat;

    ImageId result = GetEdgeImageWithOffset(edgeObject, kTunnels[EnumValue(type)].imageOffset)
                         .WithIndexOffset(edge == EDGE_BOTTOMRIGHT ? 2 : 0);

    return result;
}

static uint8_t ViewportSurfacePaintSetupGetRelativeSlope(const SurfaceElement& surfaceElement, int32_t rotation)
{
    const uint8_t slope = surfaceElement.getSlope();
    const uint8_t slopeHeight = slope & kTileSlopeDiagonalFlag;
    uint16_t slopeCorners = (slope & kTileSlopeRaisedCornersMask) << rotation;
    slopeCorners = ((slopeCorners >> 4) | slopeCorners) & 0x0F;
    return slopeHeight | slopeCorners;
}

static bool TileIsInsideClipView(const TileDescriptor& tile)
{
    if (tile.tile_element == nullptr)
        return false;

    if (tile.tile_element->getBaseZ() > gClipHeight * kCoordsZStep)
        return false;

    auto coords = tile.tile_coords.toCoordsXY();
    if (coords.x < gClipSelectionA.x || coords.x > gClipSelectionB.x)
        return false;
    if (coords.y < gClipSelectionA.y || coords.y > gClipSelectionB.y)
        return false;

    return true;
}

static void ViewportSurfaceDrawTileSideBottom(
    PaintSession& session, enum edge_t edge, uint16_t height, const TerrainEdgeObject* edgeObject, const TileDescriptor& self,
    const TileDescriptor& neighbour, bool isWater)
{
    PROFILED_FUNCTION();

    // From big Z to tiny Z
    height /= kCoordsZPerTinyZ;
    int16_t cornerHeight1, neighbourCornerHeight1, cornerHeight2, neighbourCornerHeight2;

    CoordsXY offset = { 0, 0 };
    CoordsXY bounds = { 0, 0 };
    CoordsXY tunnelBounds = { 1, 1 };
    CoordsXY tunnelTopBoundBoxOffset = { 0, 0 };

    switch (edge)
    {
        case EDGE_BOTTOMLEFT:
            cornerHeight1 = self.corner_heights.left;
            cornerHeight2 = self.corner_heights.bottom;

            neighbourCornerHeight1 = neighbour.corner_heights.top;
            neighbourCornerHeight2 = neighbour.corner_heights.right;

            offset.x = 30;
            bounds.y = 30;
            tunnelBounds.x = 32;
            tunnelTopBoundBoxOffset.y = 31;
            break;

        case EDGE_BOTTOMRIGHT:
            cornerHeight1 = self.corner_heights.right;
            cornerHeight2 = self.corner_heights.bottom;

            neighbourCornerHeight1 = neighbour.corner_heights.top;
            neighbourCornerHeight2 = neighbour.corner_heights.left;

            offset.y = 30;
            bounds.x = 30;
            tunnelBounds.y = 32;
            tunnelTopBoundBoxOffset.x = 31;
            break;

        default:
            return;
    }

    bool neighbourIsClippedAway = (session.ViewFlags & VIEWPORT_FLAG_CLIP_VIEW) && !TileIsInsideClipView(neighbour);

    if (neighbour.tile_element == nullptr || neighbourIsClippedAway)
    {
        // The neighbour tile doesn't exist or isn't drawn - assume minimum height to draw full edges
        neighbourCornerHeight2 = kMinimumLandHeight / 2;
        neighbourCornerHeight1 = kMinimumLandHeight / 2;
    }

    if (isWater && neighbour.tile_element != nullptr)
    {
        auto waterHeight = neighbour.tile_element->asSurface()->getWaterHeight() / (kCoordsZStep * 2);
        if (waterHeight == height && !neighbourIsClippedAway)
        {
            // Don't draw the edge when the neighbour's water level is the same
            return;
        }

        const int16_t minimumNeighbourCornerHeight = std::min(neighbourCornerHeight1, neighbourCornerHeight2);
        neighbourCornerHeight1 = std::max(cornerHeight1, minimumNeighbourCornerHeight);
        neighbourCornerHeight2 = std::max(cornerHeight2, minimumNeighbourCornerHeight);

        cornerHeight1 = height;
        cornerHeight2 = height;
    }

    if (cornerHeight1 <= neighbourCornerHeight1 && cornerHeight2 <= neighbourCornerHeight2)
    {
        // The edge is not visible behind the neighbour's slope
        return;
    }

    auto baseImageId = GetEdgeImage(edgeObject, 0);
    if (session.ViewFlags & VIEWPORT_FLAG_UNDERGROUND_INSIDE)
    {
        baseImageId = GetEdgeImage(edgeObject, 1);
    }

    if (edge == EDGE_BOTTOMRIGHT)
    {
        baseImageId = baseImageId.WithIndexOffset(5);
    }

    uint8_t curHeight = std::min(neighbourCornerHeight1, neighbourCornerHeight2);
    if (neighbourCornerHeight2 != neighbourCornerHeight1)
    {
        // If bottom part of edge isn't straight, add a filler
        uint32_t image_offset = 3;

        if (neighbourCornerHeight2 >= neighbourCornerHeight1)
        {
            image_offset = 4;
        }

        if (curHeight != cornerHeight1 && curHeight != cornerHeight2)
        {
            auto imageId = baseImageId.WithIndexOffset(image_offset);
            PaintAddImageAsParent(session, imageId, { offset, curHeight * kCoordsZPerTinyZ }, { bounds, 15 });
            curHeight++;
        }
    }

    neighbourCornerHeight1 = cornerHeight2;

    const auto lowestCornerHeight = std::min(cornerHeight1, cornerHeight2);

    const auto& tunnels = edge == EDGE_BOTTOMLEFT ? session.LeftTunnels : session.RightTunnels;
    for (auto& tunnel : tunnels)
    {
        if (curHeight > tunnel.height || tunnel.height >= lowestCornerHeight)
        {
            continue;
        }

        const auto tdOriginal = kTunnels[EnumValue(tunnel.type)];

        // Draw land edges up to the bottom of the tunnel
        while (curHeight < tunnel.height)
        {
            const auto boundBoxZ = curHeight == tunnel.height - 1 ? tdOriginal.lowerEdgeBoundingBoxZ : kCoordsZPerTinyZ - 1;
            PaintAddImageAsParent(session, baseImageId, { offset, curHeight * kCoordsZPerTinyZ }, { bounds, boundBoxZ });
            curHeight++;
        }

        auto tunnelType = tunnel.type;
        auto td = tdOriginal;
        uint8_t tunnelHeight = td.height;
        int16_t zOffset = curHeight;

        // When dealing with flat land but a sloped track, we fall back to the non-sloped variant.
        if ((zOffset + tunnelHeight) > neighbourCornerHeight1 || (zOffset + tunnelHeight) > cornerHeight1)
        {
            tunnelType = td.lowClearanceAlternative;
            td = kTunnels[EnumValue(tunnelType)];
        }

        zOffset *= 16;

        const int8_t boundBoxLengthBase = (tdOriginal.boundBoxLength + (td.height - tdOriginal.height)) * 16;

        int16_t boundBoxOffsetZ = zOffset + tdOriginal.boundBoxZOffset;
        int8_t boundBoxLength = boundBoxLengthBase;
        if (boundBoxOffsetZ < 16)
        {
            boundBoxOffsetZ += 16;
            boundBoxLength -= 16;
        }

        auto imageId = GetTunnelImage(edgeObject, tunnelType, edge);
        PaintAddImageAsParent(
            session, imageId, { offset, zOffset }, { { 0, 0, boundBoxOffsetZ }, { tunnelBounds, boundBoxLength - 1 } });

        boundBoxOffsetZ = curHeight * kCoordsZPerTinyZ;
        boundBoxLength = boundBoxLengthBase;
        boundBoxOffsetZ += tdOriginal.boundBoxZOffset;
        if (boundBoxOffsetZ == 0)
        {
            boundBoxOffsetZ += 16;
            boundBoxLength -= 16;
        }

        imageId = GetTunnelImage(edgeObject, tunnelType, edge).WithIndexOffset(1);
        PaintAddImageAsParent(
            session, imageId, { offset, curHeight * kCoordsZPerTinyZ },
            { { tunnelTopBoundBoxOffset, boundBoxOffsetZ }, { tunnelBounds, boundBoxLength - 1 } });

        curHeight += td.height;
    }

    // Draw land edges up to the lowest corner
    while (curHeight < lowestCornerHeight)
    {
        PaintAddImageAsParent(session, baseImageId, { offset, curHeight * kCoordsZPerTinyZ }, { bounds, kCoordsZPerTinyZ - 1 });
        curHeight++;
    }

    // Draw filler edges if the top edge is not straight
    if (curHeight < cornerHeight1 || curHeight < cornerHeight2)
    {
        const uint32_t imageOffset = curHeight >= cornerHeight1 ? 2 : 1;
        const auto imageId = baseImageId.WithIndexOffset(imageOffset);
        PaintAddImageAsParent(session, imageId, { offset, curHeight * kCoordsZPerTinyZ }, { bounds, kCoordsZPerTinyZ - 1 });
    }
}

static void ViewportSurfaceDrawTileSideTop(
    PaintSession& session, enum edge_t edge, uint16_t height, const TerrainEdgeObject* edgeObject, const TileDescriptor& self,
    const TileDescriptor& neighbour, bool isWater)
{
    PROFILED_FUNCTION();

    // From big Z to tiny Z
    height /= kCoordsZPerTinyZ;

    int16_t cornerHeight1, neighbourCornerHeight1, cornerHeight2, neighbourCornerHeight2;

    CoordsXY offset = { 0, 0 };
    CoordsXY bounds = { 0, 0 };

    switch (edge)
    {
        case EDGE_TOPLEFT:
            cornerHeight1 = self.corner_heights.top;
            cornerHeight2 = self.corner_heights.left;

            neighbourCornerHeight1 = neighbour.corner_heights.right;
            neighbourCornerHeight2 = neighbour.corner_heights.bottom;

            offset.y = -2;
            bounds.x = 30;
            break;

        case EDGE_TOPRIGHT:
            cornerHeight1 = self.corner_heights.top;
            cornerHeight2 = self.corner_heights.right;

            neighbourCornerHeight1 = neighbour.corner_heights.left;
            neighbourCornerHeight2 = neighbour.corner_heights.bottom;

            offset.x = -2;
            bounds.y = 30;
            break;

        default:
            return;
    }

    // save ecx
    if (neighbour.tile_element == nullptr)
    {
        neighbourCornerHeight1 = 1;
        neighbourCornerHeight2 = 1;
    }
    else
    {
        if (isWater)
        {
            auto waterHeight = neighbour.tile_element->asSurface()->getWaterHeight() / (kCoordsZStep * 2);
            if (height == waterHeight)
            {
                return;
            }

            neighbourCornerHeight1 = std::max(cornerHeight1, neighbourCornerHeight1);
            neighbourCornerHeight2 = std::max(cornerHeight2, neighbourCornerHeight2);

            cornerHeight1 = height;
            cornerHeight2 = height;
        }
    }

    if (cornerHeight1 <= neighbourCornerHeight1 && cornerHeight2 <= neighbourCornerHeight2)
    {
        return;
    }

    ImageId baseImageId;
    if (isWater)
    {
        baseImageId = GetEdgeImage(edgeObject, 2); // var_08
        if (session.ViewFlags & VIEWPORT_FLAG_UNDERGROUND_INSIDE)
        {
            baseImageId = GetEdgeImage(edgeObject, 1); // var_04
        }
        baseImageId = baseImageId.WithIndexOffset(edge == EDGE_TOPLEFT ? 5 : 0);

        offset.x = 0;
        offset.y = 0;
    }
    else
    {
        if (!(session.ViewFlags & VIEWPORT_FLAG_UNDERGROUND_INSIDE))
        {
            const uint8_t incline = (cornerHeight2 - cornerHeight1) + 1;
            const auto imageId = GetEdgeImage(edgeObject, 3).WithIndexOffset((edge == EDGE_TOPLEFT ? 3 : 0) + incline);
            const int16_t y = (height - cornerHeight1) * kCoordsZPerTinyZ;
            PaintAttachToPreviousPS(session, imageId, 0, y);
            return;
        }
        baseImageId = GetEdgeImage(edgeObject, 1).WithIndexOffset(edge == EDGE_TOPLEFT ? 5 : 0);
    }

    uint8_t cur_height = std::min(neighbourCornerHeight2, neighbourCornerHeight1);
    if (neighbourCornerHeight2 != neighbourCornerHeight1)
    {
        // neighbour tile corners aren't level
        uint32_t image_offset = 3;
        if (neighbourCornerHeight2 > neighbourCornerHeight1)
        {
            image_offset = 4;
        }

        if (cur_height != cornerHeight1 && cur_height != cornerHeight2)
        {
            auto imageId = baseImageId.WithIndexOffset(image_offset);
            PaintAddImageAsParent(
                session, imageId, { offset.x, offset.y, cur_height * kCoordsZPerTinyZ }, { bounds.x, bounds.y, 15 });
            cur_height++;
        }
    }

    neighbourCornerHeight1 = cornerHeight2;

    while (cur_height < cornerHeight1 && cur_height < neighbourCornerHeight1)
    {
        PaintAddImageAsParent(session, baseImageId, { offset, cur_height * kCoordsZPerTinyZ }, { bounds, 15 });
        cur_height++;
    }

    uint32_t image_offset = 1;
    if (cur_height >= cornerHeight1)
    {
        image_offset = 2;

        if (cur_height >= neighbourCornerHeight1)
        {
            return;
        }
    }

    auto imageId = baseImageId.WithIndexOffset(image_offset);
    PaintAddImageAsParent(session, imageId, { offset, cur_height * kCoordsZPerTinyZ }, { bounds, 15 });
}


#undef PaintAddImageAsParent
#undef PaintAttachToPreviousPS

static void InvokeFrozenSideSequence(PaintSession& session, uint16_t height, const TerrainEdgeObject* edgeObject,
    const TileDescriptor& selfDescriptor, const TileDescriptor* tileDescriptors)
{
        ViewportSurfaceDrawTileSideTop(session, EDGE_TOPLEFT, height, edgeObject, selfDescriptor, tileDescriptors[2], false);
        ViewportSurfaceDrawTileSideTop(session, EDGE_TOPRIGHT, height, edgeObject, selfDescriptor, tileDescriptors[3], false);
        ViewportSurfaceDrawTileSideBottom(
            session, EDGE_BOTTOMLEFT, height, edgeObject, selfDescriptor, tileDescriptors[0], false);
        ViewportSurfaceDrawTileSideBottom(
            session, EDGE_BOTTOMRIGHT, height, edgeObject, selfDescriptor, tileDescriptors[1], false);
}
