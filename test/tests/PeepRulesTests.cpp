// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <gtest/gtest.h>
#include "RetainedPeepTestHelpers.h"

#include <openrct2-renderer/gpu/PeepRules.h>
#include <openrct2/drawing/Colour.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/entity/Staff.h>
#include <openrct2/object/PeepAnimationsObject.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/peep/PeepSpriteIds.h>
#include <openrct2/profiling/Profiling.h>
#include <array>
#include <vector>

namespace Rules = OpenRCT2::Ui::Gpu::Peeps;
using namespace OpenRCT2;
using namespace OpenRCT2::Drawing::Test;

namespace FrozenPeepOracle
{
    using Direction = uint8_t;
    struct BaseImageAndOffset { ImageIndex baseImageId; uint8_t offset; PeepAnimationType actionAnimationGroup; };
    struct Trace { ImageId image; CoordsXYZ offset; BoundBoxXYZ bounds; PaintStruct* parent; };
    static std::vector<Trace> trace;
    static std::array<PaintStruct, 2> entries;
    static bool bodyVisible = true;
    static bool accessoryVisible = true;
    static uint32_t allocationAttempt = 0;
    static constexpr uint32_t kBase = 30000;
    static constexpr uint32_t kGroups = 33;
    struct Animations
    {
        std::array<PeepAnimation, kGroups * 37> rows;
        Animations()
        {
            for (size_t i = 0; i < rows.size(); ++i)
                rows[i].baseImage = kBase + static_cast<uint32_t>(i) * 2048u;
        }
        const PeepAnimation& GetPeepAnimation(PeepAnimationGroup group, PeepAnimationType type) const
        { return rows.at(static_cast<size_t>(group) * 37 + static_cast<size_t>(type)); }
    };
    static Animations animations;
    struct Manager
    {
        template<typename T> Animations* GetLoadedObject(ObjectEntryIndex) { return &animations; }
    };
    struct Context { Manager manager; Manager& GetObjectManager() { return manager; } };
    static Context context;
    static Context* GetContext() { return &context; }
    static void PaintStaffLightingEffects(const Staff&) {} // Separate LightFX gate; no lighting claim.

    static PaintStruct* CreateNormalPaintStruct(PaintSession& session, ImageId image,
        const CoordsXYZ& offset, const BoundBoxXYZ& box)
    {
        const bool visible = allocationAttempt++ == 0 ? bodyVisible : accessoryVisible;
        if (!visible) return nullptr;
        auto* parent = session.LastPS;
        auto* entry = &entries.at(trace.size());
        *entry = {};
        session.LastPS = entry;
        trace.push_back({image, offset, box, parent});
        return entry;
    }
    static PaintStruct* PaintAddImageAsParent(PaintSession& session, ImageId image,
        const CoordsXYZ& offset, const BoundBoxXYZ& box)
    {
        session.LastPS = nullptr;
        return CreateNormalPaintStruct(session, image, offset, box);
    }
    // Qualify the parent stub and rename the child helper to prevent ADL from
    // selecting the live global paint functions. The frozen artifact stays exact.
#define PaintAddImageAsParent(...) ::FrozenPeepOracle::PaintAddImageAsParent(__VA_ARGS__)
#define PaintAddImageAsChild FrozenAddImageAsChild
// Adapt read-only field spelling while keeping the independently frozen painter source byte-identical.
#define tShirtColour getTShirtColour()
#define trousersColour getTrousersColour()
#define balloonColour getBalloonColour()
#define umbrellaColour getUmbrellaColour()
#define hatColour getHatColour()
#include "../peep-parity/FrozenPeepRules.inc"
#undef hatColour
#undef umbrellaColour
#undef balloonColour
#undef trousersColour
#undef tShirtColour
#undef PaintAddImageAsChild
#undef PaintAddImageAsParent

    static void Reset(PaintSession& session)
    {
        trace.clear();
        allocationAttempt = 0;
        bodyVisible = accessoryVisible = true;
        session.LastPS = nullptr;
    }
}

namespace
{
    Rules::PeepRaw Raw()
    {
        Rules::PeepRaw raw{};
        raw.id = 7; raw.generation = 1; raw.flags = 1;
        raw.objectIndex = 4; raw.objectGeneration = 19;
        raw.colours = 3u | (7u << 8u);
        raw.accessoryColours = 11u | (13u << 8u) | (17u << 16u);
        return raw;
    }
    Rules::PeepAnimationDescriptor Table()
    {
        return {4, 19, 33, 0, FrozenPeepOracle::kBase, 33 * 37 * 2048, 0, 0};
    }
    void Populate(Peep& peep, const Rules::PeepRaw& raw)
    {
        peep.x = static_cast<int16_t>(raw.x); peep.y = static_cast<int16_t>(raw.y); peep.z = static_cast<int16_t>(raw.z);
        peep.orientation = static_cast<uint8_t>(raw.orientation);
        peep.action = static_cast<PeepActionType>(raw.action);
        peep.animationGroup = static_cast<PeepAnimationGroup>(raw.animationGroup);
        peep.animationType = static_cast<PeepAnimationType>(raw.animationType);
        peep.nextAnimationType = static_cast<PeepAnimationType>(raw.nextAnimationType);
        peep.animationImageIdOffset = static_cast<uint8_t>(raw.frameOffset);
        peep.animationObjectIndex = static_cast<ObjectEntryIndex>(raw.objectIndex);
        peep.setTShirtColour(static_cast<Drawing::Colour>(raw.colours & 255u));
        peep.setTrousersColour(static_cast<Drawing::Colour>((raw.colours >> 8u) & 255u));
    }
}

TEST(PeepRulesTest, IndependentFieldSchemaReconstructsEveryRawScalar)
{
    auto raw = Raw();
    raw.x = -31; raw.y = 69; raw.z = 24;
    raw.previousX = -33; raw.previousY = 67; raw.previousZ = 23;
    raw.sourceTick = 0; raw.previousTick = UINT32_MAX;
    raw.flags = 1u | 2u | 4u | (17u << 8);
    raw.action = 26; raw.animationGroup = 15; raw.animationType = 7; raw.nextAnimationType = 2;
    raw.orientation = 31; raw.frameOffset = 255; raw.width = 17; raw.heightMin = 49; raw.heightMax = 12;
    const auto fields = Drawing::SplitRetainedPeepRecord(raw);
    EXPECT_EQ(Drawing::JoinRetainedPeepFields(fields), raw);
    EXPECT_EQ(Rules::peepJoinFields(fields), raw);
}

TEST(PeepRulesTest, SelectorsAndRemapsMatchFrozenGuestAndStaffBodies)
{
    PaintSession session{};
    Guest guest{};
    Staff staff{};
    auto raw = Raw();
    const auto table = Table();
    for (uint32_t isStaff = 0; isStaff < 2; ++isStaff)
    for (uint32_t rotation = 0; rotation < 4; ++rotation)
    for (uint32_t orientation = 0; orientation < 32; ++orientation)
    for (uint32_t group : {0u, 5u, 7u, 15u, 32u})
    for (uint32_t type : {0u, 2u, 7u, 11u, 36u})
    for (uint32_t action : {0u, 8u, 11u, 26u, 254u, 255u})
    for (uint32_t frame : {0u, 1u, 5u, 6u, 31u, 255u})
    {
        raw.flags = 1u | (isStaff << 1u);
        raw.orientation = orientation; raw.animationGroup = group;
        raw.animationType = type; raw.nextAnimationType = (type + 3) % 37;
        raw.action = action; raw.frameOffset = frame;
        const auto address = Rules::peepFactAddress(raw, table, 33 * 37);
        ASSERT_LT(address, 33u * 37u);
        const Rules::PeepAnimationFact fact{FrozenPeepOracle::animations.rows[address].baseImage, 1, 0, 0};
        const auto selected = Rules::peepSelect(raw, table, fact, rotation, 33 * 37);
        ASSERT_EQ(selected.error, 0u);
        FrozenPeepOracle::Reset(session);
        const auto imageOrientation = static_cast<int32_t>((rotation * 8u + orientation) & 31u);
        if (isStaff != 0)
        {
            Populate(staff, raw);
            FrozenPeepOracle::PaintStaff(session, staff, imageOrientation);
        }
        else
        {
            Populate(guest, raw);
            guest.setHatColour(static_cast<Drawing::Colour>(11));
            guest.setBalloonColour(static_cast<Drawing::Colour>(13));
            guest.setUmbrellaColour(static_cast<Drawing::Colour>(17));
            FrozenPeepOracle::PaintGuest(session, guest, imageOrientation);
        }
        ASSERT_EQ(FrozenPeepOracle::trace.size(), 1u + selected.childPresent);
        const auto& parent = FrozenPeepOracle::trace[0];
        ASSERT_EQ(selected.parentImage, parent.image.GetIndex());
        ASSERT_EQ(selected.parentPrimary, static_cast<uint32_t>(parent.image.GetPrimary()));
        ASSERT_EQ(selected.parentRemapCount, parent.image.HasSecondary() ? 2u : 1u);
        if (parent.image.HasSecondary())
            ASSERT_EQ(selected.parentSecondary, static_cast<uint32_t>(parent.image.GetSecondary()));
        ASSERT_EQ(parent.offset, CoordsXYZ(0, 0, raw.z));
        ASSERT_EQ(parent.bounds.offset, CoordsXYZ(0, 0, raw.z + 5));
        ASSERT_EQ(parent.bounds.length, CoordsXYZ(1, 1, 11));
        if (selected.childPresent)
        {
            const auto& child = FrozenPeepOracle::trace[1];
            ASSERT_EQ(selected.childImage, child.image.GetIndex());
            ASSERT_EQ(selected.childPrimary, static_cast<uint32_t>(child.image.GetPrimary()));
            ASSERT_EQ(child.parent, &FrozenPeepOracle::entries[0]);
        }
    }
}

TEST(PeepRulesTest, ProjectionAndRotationSpecificBoundsMatchFrozenFunctions)
{
    auto raw = Raw();
    for (const auto pos : {CoordsXYZ{0,0,0}, CoordsXYZ{31,32,9}, CoordsXYZ{-33,16,27},
                          CoordsXYZ{15,-64,-7}, CoordsXYZ{-63,-2,255}, CoordsXYZ{8191,8190,2040}})
    for (uint32_t rotation = 0; rotation < 4; ++rotation)
    for (int zoom = -2; zoom <= 2; ++zoom)
    {
        raw.x = pos.x; raw.y = pos.y; raw.z = pos.z;
        const auto actual = Rules::peepProject(raw, rotation, zoom);
        ASSERT_EQ(actual.error, 0u);
        const auto expected = FrozenPeepOracle::Translate3DTo2DWithZ(static_cast<int32_t>(rotation), pos);
        const auto bounds = FrozenPeepOracle::kPaintPeepBoundBox(pos.z);
        const auto size = FrozenPeepOracle::RotateBoundBoxSize(bounds.length, static_cast<uint8_t>(rotation));
        EXPECT_EQ(actual.screenX, expected.x); EXPECT_EQ(actual.screenY, expected.y);
        // Mathematical floor, independently of the shader's bit-mask implementation.
        const int factor = zoom > 0 ? 1 << zoom : 1;
        const auto floorMultiple = [factor](int value) { return (value / factor - (value < 0 && value % factor != 0 ? 1 : 0)) * factor; };
        EXPECT_EQ(actual.drawX, floorMultiple(expected.x)); EXPECT_EQ(actual.drawY, floorMultiple(expected.y));
        EXPECT_EQ(actual.x, pos.x); EXPECT_EQ(actual.y, pos.y); EXPECT_EQ(actual.z, bounds.offset.z);
        EXPECT_EQ(actual.xEnd, pos.x + size.x); EXPECT_EQ(actual.yEnd, pos.y + size.y);
        EXPECT_EQ(actual.zEnd, bounds.offset.z + size.z);
    }
}

TEST(PeepRulesTest, AccessoryPromotionMatchesFrozenChildAllocation)
{
    PaintSession session{};
    Guest guest{};
    auto raw = Raw(); raw.animationGroup = 15;
    Populate(guest, raw);
    for (uint32_t body = 0; body < 2; ++body)
    for (uint32_t child = 0; child < 2; ++child)
    {
        FrozenPeepOracle::Reset(session);
        FrozenPeepOracle::bodyVisible = body != 0;
        FrozenPeepOracle::accessoryVisible = child != 0;
        FrozenPeepOracle::PaintGuest(session, guest, 0);
        ASSERT_EQ(FrozenPeepOracle::trace.size(), body + child);
        const auto expected = child == 0 ? 0u : (FrozenPeepOracle::trace.back().parent == nullptr ? 1u : 2u);
        EXPECT_EQ(Rules::peepAccessoryRelation(1, body, child), expected);
    }
}

TEST(PeepRulesTest, InvalidGenerationsRangesAndInputCannotSelectAnImage)
{
    auto raw = Raw();
    auto table = Table();
    Rules::PeepAnimationFact fact{FrozenPeepOracle::kBase, 1, 0, 0};
    const auto invalid = [&](const auto& r, const auto& t, const auto& f, uint32_t rotation, uint32_t count) {
        const auto value = Rules::peepSelect(r,t,f,rotation,count);
        EXPECT_NE(value.error, 0u); EXPECT_EQ(value.parentImage, 0xffffffffu); EXPECT_EQ(value.childPresent, 0u);
    };
    raw.generation = 0; invalid(raw,table,fact,0,33*37); raw = Raw();
    raw.objectGeneration++; invalid(raw,table,fact,0,33*37); raw = Raw();
    raw.animationGroup = 33; invalid(raw,table,fact,0,33*37); raw = Raw();
    raw.orientation = 32; invalid(raw,table,fact,0,33*37); raw = Raw();
    raw.animationType = 37; invalid(raw,table,fact,0,33*37); raw = Raw();
    raw.frameOffset = 256; invalid(raw,table,fact,0,33*37); raw = Raw();
    table.factOffset = 0xffffffffu; invalid(raw,table,fact,0,33*37); table = Table();
    // A selected first row may fit while the rest of the descriptor does not.
    invalid(raw,table,fact,0,33*37-1);
    table.groupCount = 257; invalid(raw,table,fact,0,0xffffffffu); table = Table();
    table.imageBase = 0xfffffff0u; table.imageCount = 32;
    invalid(raw,table,fact,0,33*37); table = Table();
    table.factOffset = 7;
    EXPECT_EQ(Rules::peepFactAddress(raw,table,33*37+7),7u);
    invalid(raw,table,fact,0,33*37+6); table = Table();
    invalid(raw,table,fact,4,33*37); invalid(raw,table,fact,0,0);
    fact.valid = 0; invalid(raw,table,fact,0,33*37); fact.valid = 1;
    fact.baseImage = table.imageBase + table.imageCount; invalid(raw,table,fact,0,33*37);
    fact.baseImage = 0xffffffffu; invalid(raw,table,fact,0,33*37);
    raw.x = 2147483647; EXPECT_NE(Rules::peepProject(raw,0,0).error,0u);
    raw = Raw(); EXPECT_NE(Rules::peepProject(raw,0,3).error,0u);
}

TEST(PeepRulesTest, LastDefinedImageIsAcceptedWithoutWrappingIntoUndefined)
{
    auto raw = Raw();
    auto table = Table();
    table.imageBase = UINT32_MAX - 5u;
    table.imageCount = 5;
    Rules::PeepAnimationFact fact{ table.imageBase, 1, 0, 0 };
    raw.frameOffset = 1;
    const auto last = Rules::peepSelect(raw, table, fact, 0, 33 * 37);
    EXPECT_EQ(last.error, 0u);
    EXPECT_EQ(last.parentImage, UINT32_MAX - 1u);
    raw.orientation = 8;
    EXPECT_NE(Rules::peepSelect(raw, table, fact, 0, 33 * 37).error, 0u);
}

#ifdef ENABLE_VULKAN
#include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>

namespace
{
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    constexpr uint32_t kFactPrefix = 7;
    constexpr uint32_t kFactCount = kFactPrefix + FrozenPeepOracle::kGroups * 37;
    struct PeepRuleQuery
    {
        Rules::PeepRaw raw;
        Rules::PeepAnimationDescriptor table;
        uint32_t rotation;
        int32_t zoom;
        uint32_t availableFacts;
        uint32_t bodyAllocated;
        uint32_t accessoryAllocated;
        uint32_t reserved0{}, reserved1{}, reserved2{};
    };
    // The oracle retains raw semantic test cases; only independent field groups cross the actual GPU query ABI.
    struct PeepFieldRuleQuery
    {
        Rules::PeepFields fields;
        Rules::PeepAnimationDescriptor table;
        uint32_t rotation;
        int32_t zoom;
        uint32_t availableFacts, bodyAllocated, accessoryAllocated, reserved0{}, reserved1{}, reserved2{};
    };
    static_assert(sizeof(PeepFieldRuleQuery) == 168 && offsetof(PeepFieldRuleQuery, table) == 104
        && offsetof(PeepFieldRuleQuery, rotation) == 136);
    struct PeepRuleResult
    {
        Rules::PeepSelection selection;
        Rules::PeepProjection projection;
        uint32_t factAddress{}, accessoryRelation{}, reserved0{}, reserved1{};
    };
    static_assert(sizeof(PeepRuleQuery) == 160 && offsetof(PeepRuleQuery, table) == 96
        && offsetof(PeepRuleQuery, rotation) == 128);
    static_assert(sizeof(PeepRuleResult) == 112 && offsetof(PeepRuleResult, projection) == 48
        && offsetof(PeepRuleResult, factAddress) == 96);
    using RuleContract = std::array<uint32_t, 28>;
    RuleContract Contract(const PeepRuleResult& value)
    {
        RuleContract result;
        std::memcpy(result.data(), &value, sizeof(value));
        return result;
    }
    PeepRuleQuery Query()
    {
        auto table = Table();
        table.factOffset = kFactPrefix;
        return { Raw(), table, 0, 0, kFactCount, 1, 1 };
    }
    class VulkanPeepRulesTest : public testing::Test
    {
    protected:
        std::shared_ptr<Vulkan::DeviceContext> context;
        std::unique_ptr<Vulkan::SubmissionSlots> slots;
        VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkShaderModule module = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkDeviceSize alignment = 16;
        uint64_t queryCount = 0;
        uint64_t dispatchCount = 0;
        std::filesystem::path artifacts;
        std::unique_ptr<PaintSession> oracleSession = std::make_unique<PaintSession>();

        static void Check(VkResult result, const char* operation)
        {
            if (result != VK_SUCCESS)
                throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
        }
        void SetUp() override
        {
            const auto* shader = std::getenv("OPENRCT2_PEEP_RULE_PROBE_SPV");
            const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
            if (shader == nullptr || *shader == 0)
            {
                if (required != nullptr && std::string(required) == "1")
                    FAIL() << "Required peep probe needs explicit receipt-pinned OPENRCT2_PEEP_RULE_PROBE_SPV";
                GTEST_SKIP() << "No explicit peep rule probe binary supplied";
            }
            try
            {
                context = Vulkan::DeviceContext::CreateGraphicsOnly();
                slots = std::make_unique<Vulkan::SubmissionSlots>(context, 2 * 1024 * 1024, 1);
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(context->GetPhysicalDevice(), &properties);
                alignment = std::max<VkDeviceSize>(16, properties.limits.minStorageBufferOffsetAlignment);
                ASSERT_LE(alignment, 4096u);
                ASSERT_GE(properties.limits.maxStorageBufferRange, 512 * 1024u);
                auto device = context->GetDevice();
                const std::array<VkDescriptorSetLayoutBinding, 3> bindings = { {
                    { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                    { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                    { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                } };
                const VkDescriptorSetLayoutCreateInfo descriptorInfo{
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .bindingCount = 3, .pBindings = bindings.data() };
                Check(vkCreateDescriptorSetLayout(device, &descriptorInfo, nullptr, &descriptorLayout), "descriptor layout");
                const VkPushConstantRange constantRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) };
                const VkPipelineLayoutCreateInfo layoutInfo{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                    .setLayoutCount = 1, .pSetLayouts = &descriptorLayout,
                    .pushConstantRangeCount = 1, .pPushConstantRanges = &constantRange };
                Check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout), "pipeline layout");
                std::ifstream file(std::filesystem::path(shader), std::ios::binary | std::ios::ate);
                if (!file || file.tellg() <= 0 || static_cast<size_t>(file.tellg()) % 4 != 0)
                    throw std::runtime_error("Missing/malformed peep probe SPIR-V");
                std::vector<uint32_t> words(static_cast<size_t>(file.tellg()) / 4);
                file.seekg(0);
                file.read(reinterpret_cast<char*>(words.data()), words.size() * 4);
                if (!file || words.front() != 0x07230203u)
                    throw std::runtime_error("Invalid peep probe SPIR-V magic/read");
                const VkShaderModuleCreateInfo shaderInfo{
                    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                    .codeSize = words.size() * 4, .pCode = words.data() };
                Check(vkCreateShaderModule(device, &shaderInfo, nullptr, &module), "shader module");
                const VkComputePipelineCreateInfo pipelineInfo{
                    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                    .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                               .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main" },
                    .layout = pipelineLayout };
                {
                    auto lock = context->LockPipelineCache();
                    Check(vkCreateComputePipelines(device, context->GetPipelineCache(), 1, &pipelineInfo, nullptr, &pipeline),
                          "compute pipeline");
                }
                const VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 };
                const VkDescriptorPoolCreateInfo poolInfo{
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
                    .poolSizeCount = 1, .pPoolSizes = &poolSize };
                Check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "descriptor pool");
                const VkDescriptorSetAllocateInfo allocateInfo{
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &descriptorLayout };
                Check(vkAllocateDescriptorSets(device, &allocateInfo, &descriptorSet), "descriptor set");
                if (const auto* path = std::getenv("OPENRCT2_PEEP_RULE_ARTIFACTS"))
                {
                    artifacts = std::filesystem::path(path) / testing::UnitTest::GetInstance()->current_test_info()->name();
                    if (std::filesystem::exists(artifacts))
                        throw std::runtime_error("Peep artifact directory must be fresh");
                    std::filesystem::create_directories(artifacts);
                }
            }
            catch (const std::exception& error)
            {
                FAIL() << error.what(); // An explicit shader request must never silently skip device/pipeline errors.
            }
        }
        void TearDown() override
        {
            RecordProperty("peepQueryCount", std::to_string(queryCount));
            RecordProperty("peepDispatchCount", std::to_string(dispatchCount));
            slots.reset(); // Own fence/domain completes before destroying its pipeline/descriptors.
            if (context)
            {
                auto device = context->GetDevice();
                vkDestroyDescriptorPool(device, descriptorPool, nullptr);
                vkDestroyPipeline(device, pipeline, nullptr);
                vkDestroyShaderModule(device, module, nullptr);
                vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
                vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            }
            context.reset();
        }

        // Expected images/remaps/parts and bounds come from the byte-pinned
        // painter bodies, never from the production GLSL or its C++ aliases.
        PeepRuleResult Expected(const PeepRuleQuery& q)
        {
            PeepRuleResult result{};
            auto& selection = result.selection;
            selection.parentImage = selection.childImage = UINT32_MAX;
            const auto& raw = q.raw;
            const auto direction = static_cast<uint8_t>(((q.rotation * 8u + raw.orientation) % 32u) / 8u);
            FrozenPeepOracle::Reset(*oracleSession);
            Guest guest{};
            Staff staff{};
            if ((raw.flags & 2u) != 0)
            {
                Populate(staff, raw);
                FrozenPeepOracle::PaintStaff(*oracleSession, staff, direction * 8);
            }
            else
            {
                Populate(guest, raw);
                guest.setHatColour(static_cast<Drawing::Colour>(raw.accessoryColours & 255u));
                guest.setBalloonColour(static_cast<Drawing::Colour>((raw.accessoryColours >> 8u) & 255u));
                guest.setUmbrellaColour(static_cast<Drawing::Colour>((raw.accessoryColours >> 16u) & 255u));
                FrozenPeepOracle::PaintGuest(*oracleSession, guest, direction * 8);
            }
            const Peep& peep = (raw.flags & 2u) != 0 ? static_cast<const Peep&>(staff) : static_cast<const Peep&>(guest);
            const auto frame = FrozenPeepOracle::PaintPeepGetBaseImageAndOffset(peep, direction);
            selection.direction = direction;
            selection.animationType = static_cast<uint32_t>(frame.actionAnimationGroup);
            selection.frameOffset = frame.offset;
            const auto& body = FrozenPeepOracle::trace.at(0);
            selection.parentImage = body.image.GetIndex();
            selection.parentPrimary = static_cast<uint32_t>(body.image.GetPrimary());
            selection.parentRemapCount = body.image.HasSecondary() ? 2u : 1u;
            selection.parentSecondary = body.image.HasSecondary() ? static_cast<uint32_t>(body.image.GetSecondary()) : 0u;
            if (FrozenPeepOracle::trace.size() == 2)
            {
                selection.childPresent = 1;
                selection.childImage = FrozenPeepOracle::trace[1].image.GetIndex();
                selection.childPrimary = static_cast<uint32_t>(FrozenPeepOracle::trace[1].image.GetPrimary());
            }
            const auto pos = CoordsXYZ{ raw.x, raw.y, raw.z };
            const auto screen = FrozenPeepOracle::Translate3DTo2DWithZ(q.rotation, pos);
            const auto bounds = FrozenPeepOracle::kPaintPeepBoundBox(raw.z);
            const auto length = FrozenPeepOracle::RotateBoundBoxSize(bounds.length, static_cast<uint8_t>(q.rotation));
            const int factor = q.zoom > 0 ? 1 << q.zoom : 1;
            const auto floorMultiple = [factor](int value) {
                return (value / factor - (value < 0 && value % factor != 0 ? 1 : 0)) * factor;
            };
            result.projection = { screen.x, screen.y, floorMultiple(screen.x), floorMultiple(screen.y),
                pos.x, pos.y, bounds.offset.z, pos.x + length.x, pos.y + length.y, bounds.offset.z + length.z, 0, 0 };
            result.factAddress = q.table.factOffset + raw.animationGroup * 37 + selection.animationType;
            // Execute frozen child allocation again with independent cull results.
            if (selection.childPresent != 0)
            {
                FrozenPeepOracle::Reset(*oracleSession);
                FrozenPeepOracle::bodyVisible = q.bodyAllocated != 0;
                FrozenPeepOracle::accessoryVisible = q.accessoryAllocated != 0;
                FrozenPeepOracle::PaintGuest(*oracleSession, guest, direction * 8);
                if (q.accessoryAllocated != 0)
                    result.accessoryRelation = FrozenPeepOracle::trace.back().parent == nullptr ? 1 : 2;
            }
            return result;
        }
        void Append(const char* name, const void* bytes, size_t size)
        {
            if (artifacts.empty()) return;
            std::ofstream file(artifacts / name, std::ios::binary | std::ios::app);
            file.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
            if (!file) throw std::runtime_error("Peep evidence write failed");
        }
        void Dispatch(std::span<const PeepRuleQuery> queries, std::span<const PeepRuleResult> expected)
        {
            ASSERT_LE(queries.size(), 2048u);
            ASSERT_EQ(queries.size(), expected.size());
            const uint32_t count = static_cast<uint32_t>(queries.size());
            const size_t capacity = count + 128;
            auto token = slots->Begin(0, true);
            ASSERT_TRUE(token.has_value());
            std::vector<PeepFieldRuleQuery> fieldQueries;
            fieldQueries.reserve(queries.size());
            for (const auto& q : queries)
                fieldQueries.push_back({ Drawing::SplitRetainedPeepRecord(q.raw), q.table, q.rotation, q.zoom,
                    q.availableFacts, q.bodyAllocated, q.accessoryAllocated, q.reserved0, q.reserved1, q.reserved2 });
            auto input = token->upload->Allocate(capacity * sizeof(PeepFieldRuleQuery), alignment);
            auto output = token->upload->Allocate(alignment + capacity * sizeof(PeepRuleResult), alignment);
            auto facts = token->upload->Allocate(kFactCount * sizeof(Rules::PeepAnimationFact), alignment);
            ASSERT_TRUE(input); ASSERT_TRUE(output); ASSERT_TRUE(facts);
            std::memset(input.data, 0x55, input.size);
            if (!fieldQueries.empty()) std::memcpy(input.data, fieldQueries.data(), fieldQueries.size() * sizeof(PeepFieldRuleQuery));
            std::array<Rules::PeepAnimationFact, kFactCount> factRows{};
            for (uint32_t row = 0; row < FrozenPeepOracle::animations.rows.size(); ++row)
                factRows[kFactPrefix + row] = { FrozenPeepOracle::animations.rows[row].baseImage, 1, 0, 0 };
            std::memcpy(facts.data, factRows.data(), sizeof(factRows));
            std::vector<std::byte> inputBefore(input.size);
            std::memcpy(inputBefore.data(), input.data, input.size);
            const std::array<VkDescriptorBufferInfo, 3> buffers = { {
                { input.buffer, input.offset, input.size },
                { output.buffer, output.offset + alignment, output.size - alignment },
                { facts.buffer, facts.offset, facts.size },
            } };
            std::array<VkWriteDescriptorSet, 3> writes{};
            for (uint32_t i = 0; i < writes.size(); ++i)
                writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptorSet,
                    .dstBinding = i, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .pBufferInfo = &buffers[i] };
            vkUpdateDescriptorSets(context->GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            vkCmdFillBuffer(token->commandBuffer, output.buffer, output.offset, output.size, 0xa5a5a5a5u);
            const VkMemoryBarrier prepare{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
            vkCmdPipelineBarrier(token->commandBuffer, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &prepare, 0, nullptr, 0, nullptr);
            vkCmdBindPipeline(token->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(token->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdPushConstants(token->commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(count), &count);
            vkCmdDispatch(token->commandBuffer, (count + 63) / 64 + 1, 1, 1);
            const VkMemoryBarrier finish{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
            vkCmdPipelineBarrier(token->commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &finish, 0, nullptr, 0, nullptr);
            slots->Submit(*token);
            ASSERT_TRUE(slots->Wait(*token, UINT64_MAX));
            token->upload->Invalidate(output.offset, output.size);
            token->upload->Invalidate(input.offset, input.size);
            token->upload->Invalidate(facts.offset, facts.size);
            ASSERT_EQ(std::memcmp(inputBefore.data(), input.data, input.size), 0) << "Read-only input/guard mutated";
            ASSERT_EQ(std::memcmp(factRows.data(), facts.data, facts.size), 0) << "Read-only fact catalog mutated";
            const auto* bytes = static_cast<const std::byte*>(output.data);
            for (size_t i = 0; i < alignment; ++i)
                ASSERT_EQ(bytes[i], std::byte{ 0xa5 }) << "Prefix guard byte " << i;
            for (size_t i = alignment + count * sizeof(PeepRuleResult); i < output.size; ++i)
                ASSERT_EQ(bytes[i], std::byte{ 0xa5 }) << "Over-dispatch/tail guard byte " << i;
            Append("queries.bin", fieldQueries.data(), fieldQueries.size() * sizeof(PeepFieldRuleQuery));
            Append("gpu-results.bin", bytes + alignment, count * sizeof(PeepRuleResult));
            Append("expected-contract.bin", expected.data(), expected.size_bytes());
            Append("actual-contract.bin", bytes + alignment, count * sizeof(PeepRuleResult));
            for (size_t i = 0; i < queries.size(); ++i)
            {
                PeepRuleResult actual{};
                std::memcpy(&actual, bytes + alignment + i * sizeof(actual), sizeof(actual));
                EXPECT_EQ(Contract(actual), Contract(expected[i])) << "GPU query " << queryCount + i;
            }
            queryCount += count;
            dispatchCount++;
        }
    };
}

TEST_F(VulkanPeepRulesTest, PublishedFieldsAfterSkippedRevisionsMatchFrozenPainter)
{
    Drawing::RetainedPeepScene scene;
    auto raw = Raw();
    raw.x = raw.previousX = 64; raw.y = raw.previousY = 96; raw.z = raw.previousZ = 16;
    raw.sourceTick = raw.previousTick = 1;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, true, { raw }), 1));
    const auto held = scene.GetSnapshot();
    Drawing::RetainedPeepFieldConsumer consumer;
    consumer.Commit(consumer.Prepare(held));
    raw.x = raw.previousX = 66; raw.sourceTick = raw.previousTick = 2;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { raw }), 2));
    raw.frameOffset = 5; raw.colours = 9u | (11u << 8); raw.sourceTick = raw.previousTick = 3;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { raw }), 3));
    const auto delta = consumer.Prepare(scene.GetSnapshot());
    ASSERT_EQ(delta.motion.size(), 1u);
    ASSERT_EQ(delta.appearance.size(), 1u);
    ASSERT_EQ(delta.animation.size(), 1u);
    EXPECT_TRUE(delta.lifecycle.empty());
    // Model the resident field arrays receiving one coalesced absolute update per changed group.
    // This proves the selector ABI; no claim of a production Vulkan scatter/admission is made here.
    auto resident = Drawing::SplitRetainedPeepRecord(*held->TryGet(EntityId::FromUnderlying(raw.id)));
    resident.motion = delta.motion[0].value;
    resident.appearance = delta.appearance[0].value;
    resident.animation = delta.animation[0].value;
    auto q = Query();
    q.raw = Drawing::JoinRetainedPeepFields(resident);
    EXPECT_EQ(q.raw, *scene.GetSnapshot()->TryGet(EntityId::FromUnderlying(raw.id)));
    const std::array queries{ q };
    const std::array expected{ Expected(q) };
    Dispatch(queries, expected);
    EXPECT_EQ(held->TryGet(EntityId::FromUnderlying(raw.id))->x, 64);
    EXPECT_EQ(held->TryGet(EntityId::FromUnderlying(raw.id))->frameOffset, 0u);
}

TEST_F(VulkanPeepRulesTest, ActualDeviceSelectorsProjectionAndAccessoriesMatchFrozenPainter)
{
    std::vector<PeepRuleQuery> batch;
    std::vector<PeepRuleResult> expected;
    const auto flush = [&]() { Dispatch(batch, expected); batch.clear(); expected.clear(); };
    for (uint32_t staff = 0; staff < 2; ++staff)
    for (uint32_t rotation = 0; rotation < 4; ++rotation)
    for (uint32_t orientation = 0; orientation < 32; ++orientation)
    for (uint32_t group : { 0u, 5u, 7u, 15u, 32u })
    for (uint32_t action : { 0u, 8u, 11u, 26u, 254u, 255u })
    for (uint32_t type : { 0u, 2u, 7u, 11u })
    {
        auto q = Query();
        q.raw.flags = 1u | staff * 2u;
        q.raw.orientation = orientation; q.raw.animationGroup = group;
        q.raw.action = action; q.raw.animationType = q.raw.nextAnimationType = type;
        q.raw.frameOffset = std::array<uint32_t, 6>{ 0, 1, 5, 6, 31, 255 }[(orientation + action) % 6];
        q.raw.x = static_cast<int32_t>(orientation) - 33;
        q.raw.y = static_cast<int32_t>(group * 32) - 63;
        q.raw.z = static_cast<int32_t>(action) - 7;
        q.rotation = rotation; q.zoom = static_cast<int32_t>(orientation % 5) - 2;
        q.bodyAllocated = orientation & 1u; q.accessoryAllocated = (orientation >> 1u) & 1u;
        expected.push_back(Expected(q)); batch.push_back(q);
        if (batch.size() == 2048) { flush(); ASSERT_FALSE(HasFatalFailure()); }
    }
    if (!batch.empty()) flush();
    EXPECT_EQ(queryCount, 30720u);
}

TEST_F(VulkanPeepRulesTest, InvalidCatalogAndDispatchBoundariesPreserveGuards)
{
    for (size_t count : { 0u, 1u, 63u, 64u, 65u, 127u, 128u, 129u, 2048u })
    {
        const auto q = Query();
        std::vector<PeepRuleQuery> queries(count, q);
        std::vector<PeepRuleResult> expected(count, Expected(q));
        Dispatch(queries, expected);
        ASSERT_FALSE(HasFatalFailure());
    }
    std::vector<PeepRuleQuery> queries;
    std::vector<PeepRuleResult> expected;
    const auto base = Query();
    const auto valid = Expected(base);
    const auto reject = [&](PeepRuleQuery q, bool projectionInvalid, bool addressInvalid = true) {
        auto result = valid;
        result.selection = {};
        result.selection.error = 1;
        result.selection.parentImage = result.selection.childImage = UINT32_MAX;
        if (projectionInvalid) { result.projection = {}; result.projection.error = 1; }
        if (addressInvalid) result.factAddress = UINT32_MAX;
        queries.push_back(q); expected.push_back(result);
    };
    { auto q = base; q.raw.generation = 0; reject(q, true); }
    { auto q = base; q.raw.flags = 0; reject(q, true); } // Tombstone: owner must skip it before expansion.
    { auto q = base; q.raw.flags = 1u | 8u; reject(q, true); }
    { auto q = base; q.raw.flags = 1u | 0x10000u; reject(q, true); }
    // PeepState is raw semantic data in bits8..15. This selector must accept
    // and ignore it; FerrisWheel's separate owned reader consumes the state.
    for (uint32_t state : { 0u, 1u, 17u, 127u, 255u })
    {
        auto q = base; q.raw.flags |= state << 8u;
        queries.push_back(q); expected.push_back(valid);
    }
    { auto q = base; q.raw.orientation = 32; reject(q, true); }
    { auto q = base; q.raw.animationType = 37; reject(q, true); }
    { auto q = base; q.raw.objectGeneration++; reject(q, false); }
    { auto q = base; q.raw.animationGroup = 33; reject(q, false); }
    { auto q = base; q.availableFacts--; reject(q, false); }
    { auto q = base; q.table.factOffset = UINT32_MAX; reject(q, false); }
    { auto q = base; q.table.factOffset = kFactCount; q.availableFacts = UINT32_MAX; reject(q, false); }
    { auto q = base; q.table.groupCount = 257; reject(q, false); }
    { auto q = base; q.table.reserved0 = 1; reject(q, false); }
    { auto q = base; q.table.imageBase = UINT32_MAX - 15; q.table.imageCount = 32; reject(q, false); }
    { auto q = base; q.rotation = 4; reject(q, true, false); }
    { auto q = base; q.table.factOffset = 0; auto result = valid;
        result.selection = {}; result.selection.error = 1;
        result.selection.parentImage = result.selection.childImage = UINT32_MAX;
        result.factAddress = 0; queries.push_back(q); expected.push_back(result); }
    { auto q = base; q.table.imageBase++; auto result = valid;
        result.selection = {}; result.selection.error = 2;
        result.selection.parentImage = result.selection.childImage = UINT32_MAX;
        queries.push_back(q); expected.push_back(result); }
    { auto q = base; q.table.imageCount = 1; q.raw.frameOffset = 1; auto result = valid;
        result.selection = {}; result.selection.error = 2; result.selection.frameOffset = 1;
        result.selection.parentImage = result.selection.childImage = UINT32_MAX;
        queries.push_back(q); expected.push_back(result); }
    for (int zoom : { -3, 3 })
    {
        auto q = base; q.zoom = zoom;
        auto result = valid; result.projection = {}; result.projection.error = 1;
        queries.push_back(q); expected.push_back(result);
    }
    for (int position : { INT32_MIN, INT32_MAX })
    {
        auto q = base; q.raw.x = position;
        auto result = valid; result.projection = {}; result.projection.error = 1;
        queries.push_back(q); expected.push_back(result);
    }
    Dispatch(queries, expected);
}
#endif
