// Exact frozen function bodies; reviewed test-only context/emission stubs are in the test TU.

constexpr BoundBoxXYZ kPaintPeepBoundBox(int32_t z)
{
    // In the calls to PaintAddImageAsParent/PaintAddImageAsChild, we add 5 (instead of 3) to the
    // offset.z to make sure peeps are drawn on top of railways.
    return BoundBoxXYZ{ { 0, 0, z + 5 }, { 1, 1, 11 } };
}

constexpr CoordsXYZ kPaintPeepOffset(int32_t z)
{
    return CoordsXYZ{ 0, 0, z };
}

BaseImageAndOffset PaintPeepGetBaseImageAndOffset(const Peep& peep, Direction direction)
{
    PeepAnimationType actionAnimationGroup = peep.animationType;
    uint8_t imageOffset = peep.animationImageIdOffset;

    if (peep.action == PeepActionType::idle)
    {
        actionAnimationGroup = peep.nextAnimationType;
        imageOffset = 0;
    }

    auto& objManager = GetContext()->GetObjectManager();
    auto* animObj = objManager.GetLoadedObject<PeepAnimationsObject>(peep.animationObjectIndex);

    ImageIndex baseImageId = animObj->GetPeepAnimation(peep.animationGroup, actionAnimationGroup).baseImage;

    // Offset frame onto the base image, using rotation except for the 'picked up' state
    if (actionAnimationGroup != PeepAnimationType::hanging)
        baseImageId += direction + imageOffset * 4;
    else
        baseImageId += imageOffset;

    return {
        .baseImageId = baseImageId,
        .offset = imageOffset,
        .actionAnimationGroup = actionAnimationGroup,
    };
}

PaintStruct* PaintAddImageAsChild(
    PaintSession& session, const ImageId image_id, const CoordsXYZ& offset, const BoundBoxXYZ& boundBox)
{
    PaintStruct* parentPS = session.LastPS;
    if (parentPS == nullptr)
    {
        return PaintAddImageAsParent(session, image_id, offset, boundBox);
    }

    auto* ps = CreateNormalPaintStruct(session, image_id, offset, boundBox);
    if (ps == nullptr)
    {
        return nullptr;
    }

    parentPS->Children = ps;

    return ps;
}

void PaintGuestAccesories(
    PaintSession& session, const Guest& guest, uint8_t imageOffset, PeepAnimationType actionAnimationGroup, Direction direction)
{
    // Can't display any accessories whilst drowning, clapping, or throwing up
    if (guest.action == PeepActionType::drowning || guest.action == PeepActionType::clap
        || guest.action == PeepActionType::throwUp)
        return;

    // There are only 6 walking frames available for each item,
    // as well as 1 sprite for sitting and 1 for standing still.
    auto itemFrame = imageOffset % 6;
    if (actionAnimationGroup == PeepAnimationType::watchRide)
        itemFrame = 6;
    else if (actionAnimationGroup == PeepAnimationType::sittingIdle)
        itemFrame = 7;

    if (guest.animationGroup == PeepAnimationGroup::hat)
    {
        auto itemOffset = kPeepSpriteHatItemStart;
        auto imageId = ImageId(itemOffset + direction + itemFrame * 4, guest.hatColour);
        PaintAddImageAsChild(session, imageId, kPaintPeepOffset(guest.z), kPaintPeepBoundBox(guest.z));
    }
    else if (guest.animationGroup == PeepAnimationGroup::balloon)
    {
        auto itemOffset = kPeepSpriteBalloonItemStart;
        auto imageId = ImageId(itemOffset + direction + itemFrame * 4, guest.balloonColour);
        PaintAddImageAsChild(session, imageId, kPaintPeepOffset(guest.z), kPaintPeepBoundBox(guest.z));
    }
    else if (guest.animationGroup == PeepAnimationGroup::umbrella)
    {
        auto itemOffset = kPeepSpriteUmbrellaItemStart;
        auto imageId = ImageId(itemOffset + direction + itemFrame * 4, guest.umbrellaColour);
        PaintAddImageAsChild(session, imageId, kPaintPeepOffset(guest.z), kPaintPeepBoundBox(guest.z));
    }
}

void PaintGuest(PaintSession& session, const Guest& guest, int32_t orientation)
{
    PROFILED_FUNCTION();

    if (session.rt.zoom_level > ZoomLevel{ 2 })
    {
        return;
    }

    Direction direction = (orientation >> 3);

    auto baseImageData = PaintPeepGetBaseImageAndOffset(guest, direction);
    auto imageId = ImageId(baseImageData.baseImageId, guest.tShirtColour, guest.trousersColour);

    // In the following 4 calls to PaintAddImageAsParent/PaintAddImageAsChild, we add 5 (instead of 3) to the
    // bound_box_offset_z to make sure peeps are drawn on top of railways
    auto bb = kPaintPeepBoundBox(guest.z);
    PaintAddImageAsParent(session, imageId, { 0, 0, guest.z }, bb);

    PaintGuestAccesories(session, guest, baseImageData.offset, baseImageData.actionAnimationGroup, direction);
}

void PaintStaff(PaintSession& session, const Staff& staff, int32_t orientation)
{
    PROFILED_FUNCTION();

    if (session.rt.zoom_level > ZoomLevel{ 2 })
    {
        return;
    }

    PaintStaffLightingEffects(staff);

    Direction direction = (orientation >> 3);
    auto baseImageData = PaintPeepGetBaseImageAndOffset(staff, direction);
    auto imageId = ImageId(baseImageData.baseImageId, staff.tShirtColour);

    PaintAddImageAsParent(session, imageId, kPaintPeepOffset(staff.z), kPaintPeepBoundBox(staff.z));
}

static constexpr CoordsXYZ RotateBoundBoxSize(const CoordsXYZ& bbSize, const uint8_t rotation)
{
    auto output = bbSize;
    // This probably rotates the variables so they're relative to rotation 0.
    switch (rotation)
    {
        case 0:
            output.x--;
            output.y--;
            output = { output.rotate(0), output.z };
            break;
        case 1:
            output.x--;
            output = { output.rotate(3), output.z };
            break;
        case 2:
            output = { output.rotate(2), output.z };
            break;
        case 3:
            output.y--;
            output = { output.rotate(1), output.z };
            break;
    }
    return output;
}

ScreenCoordsXY Translate3DTo2DWithZ(int32_t rotation, const CoordsXYZ& pos)
    {
        auto rotated = pos.rotate(rotation);
        // Use right shift to avoid issues like #9301
        return ScreenCoordsXY{ rotated.y - rotated.x, ((rotated.x + rotated.y) >> 1) - pos.z };
    }
