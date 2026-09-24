/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

#include "../entity/EntityRegistry.h"
#include "../entity/EntityVisualLifecycle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace OpenRCT2
{
    class PeepAnimationsObject;
}

namespace OpenRCT2::Drawing
{
    constexpr uint32_t kRetainedPeepPresent = 1;
    constexpr uint32_t kRetainedPeepStaff = 2;
    constexpr uint32_t kRetainedPeepInterpolate = 4;
    // Authoritative PeepState enum; no CPU-derived visibility decision. Low flag bits retain their ABI.
    constexpr uint32_t kRetainedPeepStateShift = 8;
    constexpr uint32_t kRetainedPeepStateMask = 0xff00;
    constexpr uint32_t kRetainedPeepAnimationTypes = 37;

    // Raw completed simulation state. No selected image, screen coordinate, quad or painter depth.
    // Colours are byte lanes: shirt/trousers; hat/balloon/umbrella/assignedStaffType respectively.
    struct RetainedPeepRecord
    {
        int32_t x{}, y{}, z{};
        uint32_t id{};
        int32_t previousX{}, previousY{}, previousZ{};
        uint32_t generation{};
        uint32_t objectIndex{}, objectGeneration{}, sourceTick{}, previousTick{};
        uint32_t orientation{}, action{}, animationGroup{}, animationType{};
        uint32_t nextAnimationType{}, frameOffset{}, colours{}, accessoryColours{};
        uint32_t width{}, heightMin{}, heightMax{}, flags{};
        bool operator==(const RetainedPeepRecord&) const = default;
    };
    static_assert(std::is_trivially_copyable_v<RetainedPeepRecord>);
    static_assert(std::is_standard_layout_v<RetainedPeepRecord>);
    static_assert(sizeof(RetainedPeepRecord) == 96);
    static_assert(offsetof(RetainedPeepRecord, id) == 12);
    static_assert(offsetof(RetainedPeepRecord, previousX) == 16);
    static_assert(offsetof(RetainedPeepRecord, generation) == 28);
    static_assert(offsetof(RetainedPeepRecord, objectIndex) == 32);
    static_assert(offsetof(RetainedPeepRecord, orientation) == 48);
    static_assert(offsetof(RetainedPeepRecord, nextAnimationType) == 64);
    static_assert(offsetof(RetainedPeepRecord, width) == 80);
    static_assert(offsetof(RetainedPeepRecord, flags) == 92);

    // Independent scalar groups. Slot ID is implicit in resident arrays; updates carry ID/generation.
    // Motion timestamps describe the last actual motion/history sample, not an unrelated appearance notification.
    struct RetainedPeepLifecycle
    {
        uint32_t generation{}, flags{};
        bool operator==(const RetainedPeepLifecycle&) const = default;
    };
    struct RetainedPeepMotion
    {
        int32_t x{}, y{}, z{};
        uint32_t orientation{};
        int32_t previousX{}, previousY{}, previousZ{};
        uint32_t sourceTick{}, previousTick{}, flags{};
        bool operator==(const RetainedPeepMotion&) const = default;
    };
    struct RetainedPeepAppearance
    {
        uint32_t objectIndex{}, objectGeneration{}, colours{}, accessoryColours{};
        bool operator==(const RetainedPeepAppearance&) const = default;
    };
    struct RetainedPeepAnimation
    {
        uint32_t action{}, animationGroup{}, animationType{}, nextAnimationType{}, frameOffset{};
        uint32_t width{}, heightMin{}, heightMax{}, flags{};
        bool operator==(const RetainedPeepAnimation&) const = default;
    };
    struct RetainedPeepFields
    {
        uint32_t id{};
        RetainedPeepLifecycle lifecycle;
        RetainedPeepMotion motion;
        RetainedPeepAppearance appearance;
        RetainedPeepAnimation animation;
    };
    static_assert(sizeof(RetainedPeepLifecycle) == 8 && sizeof(RetainedPeepMotion) == 40);
    static_assert(sizeof(RetainedPeepAppearance) == 16 && sizeof(RetainedPeepAnimation) == 36);
    static_assert(sizeof(RetainedPeepFields) == 104 && offsetof(RetainedPeepFields, animation) == 68);
    RetainedPeepFields SplitRetainedPeepRecord(const RetainedPeepRecord& record) noexcept;
    RetainedPeepRecord JoinRetainedPeepFields(const RetainedPeepFields& fields) noexcept;

    constexpr size_t kRetainedPeepChunkWidth = 64;
    constexpr size_t kRetainedPeepChunkCount = (kMaxEntities + kRetainedPeepChunkWidth - 1) / kRetainedPeepChunkWidth;
    template<typename T> struct RetainedPeepFieldChunk
    {
        std::array<T, kRetainedPeepChunkWidth> values{};
        // Latest absolute values accumulate all changes since any earlier same-epoch consumer revision.
        std::array<uint64_t, kRetainedPeepChunkWidth> revisions{};
    };
    struct RetainedPeepChunk
    {
        uint64_t revision{};
        std::shared_ptr<const RetainedPeepFieldChunk<RetainedPeepLifecycle>> lifecycle;
        std::shared_ptr<const RetainedPeepFieldChunk<RetainedPeepMotion>> motion;
        std::shared_ptr<const RetainedPeepFieldChunk<RetainedPeepAppearance>> appearance;
        std::shared_ptr<const RetainedPeepFieldChunk<RetainedPeepAnimation>> animation;
    };
    struct RetainedPeepSnapshot
    {
        uint64_t epoch{}, sequence{};
        size_t count{};
        std::array<std::shared_ptr<const RetainedPeepChunk>, kRetainedPeepChunkCount> chunks{};
        std::optional<RetainedPeepRecord> TryGet(EntityId id) const noexcept;
    };
    template<typename T> struct RetainedPeepFieldUpdate
    {
        uint32_t id{}, generation{};
        T value;
    };
    struct RetainedPeepLifecycleUpdate
    {
        uint32_t id{};
        RetainedPeepLifecycle value;
    };
    static_assert(sizeof(RetainedPeepLifecycleUpdate) == 12);
    static_assert(sizeof(RetainedPeepFieldUpdate<RetainedPeepMotion>) == 48);
    static_assert(sizeof(RetainedPeepFieldUpdate<RetainedPeepAppearance>) == 24);
    static_assert(sizeof(RetainedPeepFieldUpdate<RetainedPeepAnimation>) == 44);
    struct RetainedPeepFieldDelta
    {
        uint64_t baseEpoch{}, baseSequence{};
        // Own the coherent destination revision. The world generation separately owns its matching object catalog.
        std::shared_ptr<const RetainedPeepSnapshot> snapshot;
        bool reset{};
        std::vector<RetainedPeepLifecycleUpdate> lifecycle;
        std::vector<RetainedPeepFieldUpdate<RetainedPeepMotion>> motion;
        std::vector<RetainedPeepFieldUpdate<RetainedPeepAppearance>> appearance;
        std::vector<RetainedPeepFieldUpdate<RetainedPeepAnimation>> animation;
        size_t PayloadBytes() const noexcept;
    };
    class RetainedPeepFieldConsumer
    {
        uint64_t _epoch{}, _sequence{};
    public:
        // Prepare is non-consuming. A dropped/failed GPU recording cannot lose changes or advance the base.
        RetainedPeepFieldDelta Prepare(std::shared_ptr<const RetainedPeepSnapshot> snapshot) const;
        // Commit all four groups together only after their owning submission is accepted. A stale prepared base fails.
        void Commit(const RetainedPeepFieldDelta& delta);
        uint64_t GetEpoch() const noexcept { return _epoch; }
        uint64_t GetSequence() const noexcept { return _sequence; }
    };
    struct RetainedPeepBatch
    {
        uint64_t epoch{};
        bool reset{};
        // A slot occurs at most once per group. New/reused live identities require lifecycle and all
        // three payload groups; a tombstone carries lifecycle only. Existing identities accept partials.
        // Never reset an existing same-epoch scene: that would forget its deletion tombstones.
        std::vector<RetainedPeepLifecycleUpdate> lifecycle;
        std::vector<RetainedPeepFieldUpdate<RetainedPeepMotion>> motion;
        std::vector<RetainedPeepFieldUpdate<RetainedPeepAppearance>> appearance;
        std::vector<RetainedPeepFieldUpdate<RetainedPeepAnimation>> animation;
        size_t PayloadBytes() const noexcept;
    };
    struct RetainedEntityPublicationInput
    {
        uint64_t sourceEpoch{};
        uint32_t sourceTick{};
        bool bootstrap{};
        uint64_t dirtyVisits{}, bootstrapVisits{};
        RetainedPeepBatch peeps;
        EntityVisualChangeBatch balloons;
    };

    struct RetainedPeepApplyMetrics
    {
        uint64_t changedRecords{}, clonedChunks{}, copiedRecordBytes{}, copiedRevisionBytes{}, copiedChunkMetadataBytes{};
    };
    class RetainedPeepScene
    {
        std::shared_ptr<const RetainedPeepSnapshot> _snapshot;
        RetainedPeepApplyMetrics _metrics;

    public:
        // Sequential publisher input; consumers can skip complete snapshots without losing deletions.
        // Invalid batches leave the published snapshot and metrics untouched. An input gap requires a new
        // epoch and complete reset; a same-epoch reset is rejected, including when it is otherwise complete.
        // Sequences are lifetime-monotonic, even across epochs. Entity generations use half-range ordering;
        // a producer must not coalesce >= 2^31 slot reincarnations into one delta.
        bool Apply(const RetainedPeepBatch& batch, uint64_t sequence);
        const std::shared_ptr<const RetainedPeepSnapshot>& GetSnapshot() const noexcept { return _snapshot; }
        RetainedPeepApplyMetrics GetLastApplyMetrics() const noexcept { return _metrics; }
    };

    struct RetainedPeepAnimationFact
    {
        uint32_t baseImage{}, valid{}, reserved0{}, reserved1{};
        bool operator==(const RetainedPeepAnimationFact&) const = default;
    };
    struct RetainedPeepAnimationDescriptor
    {
        uint32_t objectIndex{}, objectGeneration{}, groupCount{}, factOffset{};
        uint32_t imageBase{}, imageCount{}, reserved0{}, reserved1{};
        bool operator==(const RetainedPeepAnimationDescriptor&) const = default;
    };
    static_assert(sizeof(RetainedPeepAnimationFact) == 16);
    static_assert(sizeof(RetainedPeepAnimationDescriptor) == 32);
    static_assert(std::is_trivially_copyable_v<RetainedPeepAnimationFact>);
    static_assert(std::is_standard_layout_v<RetainedPeepAnimationFact>);
    static_assert(std::is_trivially_copyable_v<RetainedPeepAnimationDescriptor>);
    static_assert(std::is_standard_layout_v<RetainedPeepAnimationDescriptor>);
    static_assert(offsetof(RetainedPeepAnimationDescriptor, imageBase) == 16);
    struct RetainedPeepAnimationObject
    {
        RetainedPeepAnimationDescriptor descriptor;
        // Dense group-major table, 37 types/group. Invalid holes remain zero. factOffset is zero here;
        // an owned GPU catalog may flatten and relocate whole objects at publication, never per peep/frame.
        std::vector<RetainedPeepAnimationFact> facts;
    };
    struct RetainedPeepAnimationSource
    {
        uint32_t group{}, type{}, baseImage{};
    };
    std::shared_ptr<const RetainedPeepAnimationObject> BuildRetainedPeepAnimationObject(
        RetainedPeepAnimationDescriptor descriptor, std::span<const RetainedPeepAnimationSource> sources);
    // Object-load boundary only: copies static rules, never evaluates a peep's action or frame.
    std::shared_ptr<const RetainedPeepAnimationObject> CaptureRetainedPeepAnimationObject(
        const PeepAnimationsObject& object, uint32_t objectIndex, uint32_t generation);

    struct RetainedPeepAnimationSlot
    {
        uint32_t generation{};
        std::shared_ptr<const RetainedPeepAnimationObject> object; // null is a versioned unload tombstone
    };
    struct RetainedPeepAnimationCatalog
    {
        uint64_t epoch{}, sequence{};
        std::map<uint32_t, RetainedPeepAnimationSlot> slots;
    };
    struct RetainedPeepAnimationChange
    {
        uint32_t objectIndex{}, generation{};
        std::shared_ptr<const RetainedPeepAnimationObject> object;
    };
    // New immutable catalog, strong-owned object facts, no borrowed ObjectManager pointer.
    // Uses the same epoch/reset and lifetime-monotonic sequence contract as RetainedPeepScene.
    std::shared_ptr<const RetainedPeepAnimationCatalog> PublishRetainedPeepAnimationCatalog(
        const std::shared_ptr<const RetainedPeepAnimationCatalog>& previous, uint64_t epoch, uint64_t sequence,
        bool reset, std::span<const RetainedPeepAnimationChange> changes);
}
