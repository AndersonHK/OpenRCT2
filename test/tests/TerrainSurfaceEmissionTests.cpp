/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include <gtest/gtest.h>
#include <openrct2-renderer/gpu/TerrainSurfaceEmission.h>
#include <openrct2-renderer/gpu/TerrainSurfaceRules.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/object/TerrainEdgeObject.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/paint/tile_element/Paint.TileElement.h>
#include <openrct2/profiling/Profiling.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapSelection.h>
#include <openrct2/world/tile_element/Slope.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <openrct2/world/tile_element/TileElement.h>
#include <algorithm>
#include <array>
#include <memory>
#include <set>
#include <vector>

namespace Rules = OpenRCT2::Ui::Gpu::Terrain;

namespace FrozenTerrainOracle
{
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;
    static thread_local std::vector<Rules::TerrainEdgeEmission> trace;
    static constexpr uint32_t kEdgeImageBase = 1000;

    static PaintStruct* CaptureParent(PaintSession& session, ImageId image, const CoordsXYZ& offset, const BoundBoxXYZ& box)
    {
        EXPECT_EQ(box.offset, offset);
        trace.push_back({ static_cast<int>(image.GetIndex() - kEdgeImageBase), offset.x, offset.y, offset.z,
                          box.length.x, box.length.y, box.length.z, 0 });
        session.LastAttachedPS = nullptr;
        return session.AllocateNormalPaintEntry();
    }
    static PaintStruct* CaptureParent(PaintSession& session, ImageId image, const CoordsXYZ& offset, const CoordsXYZ& size)
    {
        return CaptureParent(session, image, offset, BoundBoxXYZ{ offset, size });
    }
    static bool CaptureAttached(PaintSession& session, ImageId image, int32_t x, int32_t y);

    // Exact pinned functions/table fragments, with only the two emission APIs
    // intercepted. The untouched branch/loop bodies execute in these tests.
#include "../terrain-parity/FrozenTerrainEdgeOracle.inc"

    static bool CaptureAttached(PaintSession& session, ImageId image, int32_t x, int32_t y)
    {
        trace.push_back({ static_cast<int>(image.GetIndex() - kEdgeImageBase), x, y, 0, 0, 0, 0, 1 });
        return FrozenTerrainOracle::PaintAttachToPreviousPS(session, image, x, y);
    }

    static TileDescriptor Describe(int height, int rawSlope, int rotation, const TileElement* element)
    {
        SurfaceElement surface{};
        surface.setSlope(static_cast<uint8_t>(rawSlope));
        const auto relative = ViewportSurfacePaintSetupGetRelativeSlope(surface, rotation);
        auto corners = FrozenTerrainOracle::GetSlopeRelativeCornerHeights(relative);
        corners.top += height / 16;
        corners.right += height / 16;
        corners.bottom += height / 16;
        corners.left += height / 16;
        return { {}, element, nullptr, relative, corners };
    }

    static constexpr std::array<edge_t, 4> kEdges = {
        EDGE_BOTTOMLEFT, EDGE_BOTTOMRIGHT, EDGE_TOPLEFT, EDGE_TOPRIGHT
    };
    static void InvokeEdge(PaintSession& session, int edge, int height, const TerrainEdgeObject& material,
                           const TileDescriptor& own, const TileDescriptor& neighbour)
    {
        if (edge < 2)
            ViewportSurfaceDrawTileSideBottom(session, kEdges[edge], height, &material, own, neighbour, false);
        else
            ViewportSurfaceDrawTileSideTop(session, kEdges[edge], height, &material, own, neighbour, false);
    }
}

static std::array<int, 8> Fields(const Rules::TerrainEdgeEmission& e)
{
    return { e.imageOffset, e.x, e.y, e.z, e.boundsX, e.boundsY, e.boundsZ, e.attached };
}

TEST(TerrainSurfaceEmissionTest, RotationShapeAndNeighbourCoordinatesMatchIndependentFrozenTables)
{
    for (int rotation = 0; rotation < 4; rotation++)
    {
        for (int slope = 0; slope < 16; slope++)
        {
            OpenRCT2::SurfaceElement surface{};
            surface.setSlope(static_cast<uint8_t>(slope));
            const auto relative = FrozenTerrainOracle::ViewportSurfacePaintSetupGetRelativeSlope(surface, rotation);
            EXPECT_EQ(Rules::terrainRelativeSlope(slope, rotation), relative);
            EXPECT_EQ(Rules::RelativeSlope(slope, rotation), relative);
            EXPECT_EQ(Rules::terrainShapeImageOffset(relative), FrozenTerrainOracle::Byte97B444[relative]);
            auto expected = FrozenTerrainOracle::GetSlopeRelativeCornerHeights(relative);
            const std::array<int, 4> corners = { expected.top, expected.right, expected.bottom, expected.left };
            for (int c = 0; c < 4; c++)
            {
                EXPECT_EQ(Rules::terrainCornerHeight(16, relative, c), 1 + corners[c]);
                EXPECT_EQ(static_cast<int>(Rules::kShapeRules[relative].cornerHeights[c]), corners[c]);
            }
        }
        for (int edge = 0; edge < 4; edge++)
        {
            EXPECT_EQ(Rules::terrainNeighbourX(edge, rotation) * 32,
                      FrozenTerrainOracle::kNeighbouringTileCoordOffsets[edge][rotation].x);
            EXPECT_EQ(Rules::terrainNeighbourY(edge, rotation) * 32,
                      FrozenTerrainOracle::kNeighbouringTileCoordOffsets[edge][rotation].y);
        }
    }
}

TEST(TerrainSurfaceEmissionTest, AllNonSteepNeighbourPairsMatchFrozenSideTraceInEmissionOrder)
{
    auto session = std::make_unique<PaintSession>();
    OpenRCT2::TerrainEdgeObject material;
    material.BaseImageId = FrozenTerrainOracle::kEdgeImageBase;
    OpenRCT2::TileElement element{};
    std::set<int> observedOffsets;
    size_t cases = 0;
    for (int rotation = 0; rotation < 4; rotation++)
    for (int base : { 16, 32, 48, 64 })
    for (int neighbourBase : { 16, 32, 48, 64 })
    for (int slope = 0; slope < 16; slope++)
    for (int neighbourSlope = 0; neighbourSlope < 16; neighbourSlope++)
    for (int edge = 0; edge < 4; edge++)
    {
        SCOPED_TRACE(::testing::Message() << rotation << '/' << base << '/' << neighbourBase << '/'
                     << slope << '/' << neighbourSlope << '/' << edge);
        session->paintEntries.clear();
        session->AllocateNormalPaintEntry(); // Surface base exists before side attachments.
        FrozenTerrainOracle::trace.clear();
        auto own = FrozenTerrainOracle::Describe(base, slope, rotation, &element);
        auto neighbour = FrozenTerrainOracle::Describe(neighbourBase, neighbourSlope, rotation, &element);
        FrozenTerrainOracle::InvokeEdge(*session, edge, base, material, own, neighbour);
        const auto plan = Rules::terrainPlanEdge(edge, base, slope, neighbourBase, neighbourSlope, true, rotation);
        ASSERT_EQ(static_cast<size_t>(plan.count), FrozenTerrainOracle::trace.size());
        for (int index = 0; index < plan.count; index++)
        {
            EXPECT_EQ(Fields(Rules::terrainEdgeAt(plan, index)), Fields(FrozenTerrainOracle::trace[index]));
            observedOffsets.insert(FrozenTerrainOracle::trace[index].imageOffset);
        }
        cases++;
    }
    EXPECT_EQ(cases, 65536u);
    const std::set<int> expectedOffsets = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 30, 31, 32, 33, 34, 35 };
    EXPECT_EQ(observedOffsets, expectedOffsets);
}

TEST(TerrainSurfaceEmissionTest, MissingNeighboursAndMaximumSupportedHeightMatchFrozenTrace)
{
    auto session = std::make_unique<PaintSession>();
    OpenRCT2::TerrainEdgeObject material;
    material.BaseImageId = FrozenTerrainOracle::kEdgeImageBase;
    OpenRCT2::TileElement element{};
    for (int rotation = 0; rotation < 4; rotation++)
    for (int base : { 16, 64, 4064 })
    for (int slope = 0; slope < 16; slope++)
    for (int edge = 0; edge < 4; edge++)
    {
        SCOPED_TRACE(::testing::Message() << rotation << '/' << base << '/' << slope << '/' << edge);
        session->paintEntries.clear();
        session->AllocateNormalPaintEntry();
        FrozenTerrainOracle::trace.clear();
        auto own = FrozenTerrainOracle::Describe(base, slope, rotation, &element);
        FrozenTerrainOracle::TileDescriptor missing{};
        FrozenTerrainOracle::InvokeEdge(*session, edge, base, material, own, missing);
        const auto plan = Rules::terrainPlanEdge(edge, base, slope, 0, 0, false, rotation);
        ASSERT_EQ(static_cast<size_t>(plan.count), FrozenTerrainOracle::trace.size());
        for (int index = 0; index < plan.count; index++)
            EXPECT_EQ(Fields(Rules::terrainEdgeAt(plan, index)), Fields(FrozenTerrainOracle::trace[index]));
    }
    EXPECT_FALSE(Rules::terrainRuleInputValid(4080, 1, 0));
    EXPECT_FALSE(Rules::terrainRuleInputValid(16, 16, 0));
    EXPECT_FALSE(Rules::terrainRuleInputValid(17, 0, 0));
    EXPECT_FALSE(Rules::terrainRuleInputValid(16, 0, 4));
}

TEST(TerrainSurfaceEmissionTest, RearAttachmentTraversalIsReverseOfCallOrder)
{
    auto session = std::make_unique<PaintSession>();
    OpenRCT2::TerrainEdgeObject material;
    material.BaseImageId = FrozenTerrainOracle::kEdgeImageBase;
    OpenRCT2::TileElement element{};
    for (int rotation = 0; rotation < 4; rotation++)
    for (int slope = 0; slope < 16; slope++)
    {
        session->paintEntries.clear();
        auto* base = session->AllocateNormalPaintEntry();
        FrozenTerrainOracle::trace.clear();
        auto own = FrozenTerrainOracle::Describe(64, slope, rotation, &element);
        const auto neighbour = FrozenTerrainOracle::Describe(16, 0, rotation, &element);
        const std::array<FrozenTerrainOracle::TileDescriptor, 4> neighbours = { neighbour, neighbour, neighbour, neighbour };
        FrozenTerrainOracle::InvokeFrozenSideSequence(*session, 64, &material, own, neighbours.data());
        size_t traceIndex = 0;
        for (int call = 0; call < 4; call++)
        {
            const int edge = Rules::terrainEdgeForCall(call);
            const auto plan = Rules::terrainPlanEdge(edge, 64, slope, 16, 0, true, rotation);
            for (int i = 0; i < plan.count; i++)
            {
                ASSERT_LT(traceIndex, FrozenTerrainOracle::trace.size());
                EXPECT_EQ(Fields(Rules::terrainEdgeAt(plan, i)), Fields(FrozenTerrainOracle::trace[traceIndex++]));
            }
        }
        EXPECT_EQ(traceIndex, FrozenTerrainOracle::trace.size());
        auto* attached = base->Attached;
        for (int traversal = 0; traversal < 2; traversal++)
        {
            ASSERT_NE(attached, nullptr);
            const auto plan = Rules::terrainPlanEdge(Rules::terrainRearEdgeForTraversal(traversal), 64, slope, 16, 0, true, rotation);
            const auto expected = Rules::terrainEdgeAt(plan, 0);
            EXPECT_EQ(attached->image_id.GetIndex(), material.BaseImageId + expected.imageOffset);
            EXPECT_EQ(attached->RelativePos.x, expected.x);
            EXPECT_EQ(attached->RelativePos.y, expected.y);
            attached = attached->NextEntry;
        }
        EXPECT_EQ(attached, nullptr);
    }
}

TEST(TerrainSurfaceEmissionTest, ShaderMaterialSelectorMatchesCompiledObjectLookupAddress)
{
    for (int zoom : { -1, 0, 1, 2, 3 })
    for (int grass = 0; grass < 8; grass++)
    for (int rotation = 0; rotation < 4; rotation++)
    for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++)
        EXPECT_EQ(static_cast<size_t>(Rules::terrainMaterialSelector(grass, rotation, x, y, zoom)),
                  Rules::MaterialSelector(zoom > 0 ? Rules::kAny : grass, rotation, (x & 1) | ((y & 1) << 1)));
}


#ifdef ENABLE_VULKAN
#include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
#include <openrct2/object/TerrainSurfaceObject.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>

namespace
{
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    struct alignas(16) TerrainRuleQuery
    {
        std::array<int32_t, 4> own; // base, slope, rotation, grass
        std::array<int32_t, 4> neighbour; // base, slope, present, zoom
        std::array<int32_t, 4> query; // edge, emission index, tile x/y
    };
    struct alignas(16) TerrainRuleResult
    {
        Rules::TerrainEdgePlan plan;
        Rules::TerrainEdgeEmission emission;
        std::array<int32_t, 4> metadata;
    };
    static_assert(sizeof(TerrainRuleQuery) == 48 && offsetof(TerrainRuleQuery, query) == 32);
    static_assert(sizeof(TerrainRuleResult) == 80 && offsetof(TerrainRuleResult, emission) == 32
                  && offsetof(TerrainRuleResult, metadata) == 64);
    // Compare the semantic contract; plan's other six fields are private
    // intermediate representation, not independently observable emissions.
    using RuleContract = std::array<int32_t, 14>;
    RuleContract Contract(const TerrainRuleResult& result)
    {
        const auto e = Fields(result.emission);
        RuleContract values{ result.plan.count, result.plan.edge };
        std::copy(e.begin(), e.end(), values.begin() + 2);
        std::copy(result.metadata.begin(), result.metadata.end(), values.begin() + 10);
        return values;
    }

    class VulkanTerrainSurfaceRulesTest : public testing::Test
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
        OpenRCT2::TerrainSurfaceObject selectorOracle;
        std::unique_ptr<PaintSession> oracleSession = std::make_unique<PaintSession>();
        OpenRCT2::TerrainEdgeObject edgeOracle;
        OpenRCT2::TileElement element{};

        static void Check(VkResult result, const char* operation)
        {
            if (result != VK_SUCCESS)
                throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
        }
        void SetUp() override
        {
            const auto* shader = std::getenv("OPENRCT2_TERRAIN_RULE_PROBE_SPV");
            const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
            if (shader == nullptr || *shader == 0)
            {
                if (required != nullptr && std::string(required) == "1")
                    FAIL() << "Required terrain probe needs explicit receipt-pinned OPENRCT2_TERRAIN_RULE_PROBE_SPV";
                GTEST_SKIP() << "No explicit terrain rule probe binary supplied";
            }
            try
            {
                context = Vulkan::DeviceContext::CreateGraphicsOnly();
                slots = std::make_unique<Vulkan::SubmissionSlots>(context, 1024 * 1024, 1);
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(context->GetPhysicalDevice(), &properties);
                alignment = std::max<VkDeviceSize>(16, properties.limits.minStorageBufferOffsetAlignment);
                ASSERT_LE(alignment, 4096u);
                ASSERT_GE(properties.limits.maxStorageBufferRange, 512 * 1024u);
                auto device = context->GetDevice();
                const std::array<VkDescriptorSetLayoutBinding, 2> bindings = { {
                    { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                    { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                } };
                const VkDescriptorSetLayoutCreateInfo descriptorInfo{
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .bindingCount = 2, .pBindings = bindings.data() };
                Check(vkCreateDescriptorSetLayout(device, &descriptorInfo, nullptr, &descriptorLayout), "descriptor layout");
                const VkPushConstantRange constantRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) };
                const VkPipelineLayoutCreateInfo layoutInfo{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                    .setLayoutCount = 1, .pSetLayouts = &descriptorLayout,
                    .pushConstantRangeCount = 1, .pPushConstantRanges = &constantRange };
                Check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout), "pipeline layout");
                std::ifstream file(std::filesystem::path(shader), std::ios::binary | std::ios::ate);
                if (!file || file.tellg() <= 0 || static_cast<size_t>(file.tellg()) % 4 != 0)
                    throw std::runtime_error("Missing/malformed terrain probe SPIR-V");
                std::vector<uint32_t> words(static_cast<size_t>(file.tellg()) / 4);
                file.seekg(0);
                file.read(reinterpret_cast<char*>(words.data()), words.size() * 4);
                if (!file || words.front() != 0x07230203u)
                    throw std::runtime_error("Invalid terrain probe SPIR-V magic/read");
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
                const VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
                const VkDescriptorPoolCreateInfo poolInfo{
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
                    .poolSizeCount = 1, .pPoolSizes = &poolSize };
                Check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "descriptor pool");
                const VkDescriptorSetAllocateInfo allocateInfo{
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &descriptorLayout };
                Check(vkAllocateDescriptorSets(device, &allocateInfo, &descriptorSet), "descriptor set");
                edgeOracle.BaseImageId = FrozenTerrainOracle::kEdgeImageBase;
                selectorOracle.EntryBaseImageId = 0;
                selectorOracle.DefaultEntry = 0;
                selectorOracle.Colour = OpenRCT2::Drawing::kColourNull;
                for (int length = 0; length < 9; length++)
                for (int rotation = 0; rotation < 4; rotation++)
                for (int variation = 0; variation < 4; variation++)
                    selectorOracle.SpecialEntries.push_back({ static_cast<uint8_t>((length * 4 + rotation) * 4 + variation + 1),
                        static_cast<uint8_t>(length == 8 ? 255 : length), static_cast<uint8_t>(rotation),
                        static_cast<uint8_t>(variation) });
                if (const auto* path = std::getenv("OPENRCT2_TERRAIN_RULE_ARTIFACTS"))
                {
                    artifacts = std::filesystem::path(path) / testing::UnitTest::GetInstance()->current_test_info()->name();
                    if (std::filesystem::exists(artifacts))
                        throw std::runtime_error("Terrain artifact directory must be fresh");
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
            RecordProperty("terrainQueryCount", std::to_string(queryCount));
            RecordProperty("terrainDispatchCount", std::to_string(dispatchCount));
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
        RuleContract Expected(const TerrainRuleQuery& q)
        {
            // Independent frozen edge/slope inputs; material selection uses the existing
            // public TerrainSurfaceObject API. No expected value calls terrainPlanEdge,
            // terrainEdgeAt, terrainRelativeSlope or the host-compiled new rules.
            const auto validTile = [](int z, int slope, int rotation) {
                return z >= 16 && z <= 4064 && z % 16 == 0 && slope >= 0 && slope <= 15
                    && rotation >= 0 && rotation <= 3;
            };
            bool tileValid = validTile(q.own[0], q.own[1], q.own[2])
                && (q.neighbour[2] == 0 || validTile(q.neighbour[0], q.neighbour[1], q.own[2]));
            bool edgeValid = q.query[0] >= 0 && q.query[0] < 4;
            RuleContract expected{};
            expected[1] = q.query[0];
            if (tileValid)
            {
                const auto self = FrozenTerrainOracle::Describe(q.own[0], q.own[1], q.own[2], &element);
                expected[10] = self.slope;
                expected[11] = FrozenTerrainOracle::Byte97B444[self.slope];
                auto material = selectorOracle.GetImageId(
                    { q.query[2] * 32, q.query[3] * 32 },
                    static_cast<uint8_t>(q.neighbour[3] > 0 ? 255 : q.own[3] & 7),
                    static_cast<uint8_t>(q.own[2]), 0, false, false);
                expected[12] = static_cast<int>(material.GetIndex() / 19) - 1;
                if (edgeValid)
                {
                    oracleSession->paintEntries.clear();
                    oracleSession->AllocateNormalPaintEntry();
                    FrozenTerrainOracle::trace.clear();
                    const auto neighbour = q.neighbour[2] != 0
                        ? FrozenTerrainOracle::Describe(q.neighbour[0], q.neighbour[1], q.own[2], &element)
                        : FrozenTerrainOracle::TileDescriptor{};
                    FrozenTerrainOracle::InvokeEdge(*oracleSession, q.query[0], q.own[0], edgeOracle, self, neighbour);
                    expected[0] = static_cast<int>(FrozenTerrainOracle::trace.size());
                    if (q.query[1] >= 0 && q.query[1] < expected[0])
                    {
                        auto fields = Fields(FrozenTerrainOracle::trace[q.query[1]]);
                        std::copy(fields.begin(), fields.end(), expected.begin() + 2);
                    }
                }
            }
            expected[13] = (tileValid ? 1 : 0) | (edgeValid ? 2 : 0)
                | ((q.query[1] >= 0 && q.query[1] < expected[0]) ? 4 : 0);
            return expected;
        }
        void Append(const char* name, const void* bytes, size_t size)
        {
            if (artifacts.empty())
                return;
            std::ofstream file(artifacts / name, std::ios::binary | std::ios::app);
            file.write(static_cast<const char*>(bytes), size);
            if (!file)
                throw std::runtime_error("Terrain evidence write failed");
        }
        void Dispatch(std::span<const TerrainRuleQuery> queries)
        {
            ASSERT_LE(queries.size(), 2048u);
            const uint32_t count = static_cast<uint32_t>(queries.size());
            const size_t capacity = count + 128; // Includes the deliberately over-dispatched workgroup.
            auto token = slots->Begin(0, true);
            ASSERT_TRUE(token.has_value());
            auto input = token->upload->Allocate(capacity * sizeof(TerrainRuleQuery), alignment);
            auto output = token->upload->Allocate(alignment + capacity * sizeof(TerrainRuleResult), alignment);
            ASSERT_TRUE(input); ASSERT_TRUE(output);
            std::memset(input.data, 0x55, input.size);
            if (!queries.empty())
                std::memcpy(input.data, queries.data(), queries.size_bytes());
            std::vector<std::byte> inputBefore(input.size);
            std::memcpy(inputBefore.data(), input.data, input.size);
            const std::array<VkDescriptorBufferInfo, 2> buffers = { {
                { input.buffer, input.offset, input.size },
                { output.buffer, output.offset + alignment, output.size - alignment },
            } };
            std::array<VkWriteDescriptorSet, 2> writes{};
            for (uint32_t i = 0; i < 2; i++)
                writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptorSet,
                              .dstBinding = i, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                              .pBufferInfo = &buffers[i] };
            vkUpdateDescriptorSets(context->GetDevice(), 2, writes.data(), 0, nullptr);
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
            ASSERT_EQ(std::memcmp(inputBefore.data(), input.data, input.size), 0) << "Read-only input/guard mutated";
            const auto* bytes = static_cast<const std::byte*>(output.data);
            for (size_t i = 0; i < alignment; i++)
                ASSERT_EQ(bytes[i], std::byte{ 0xa5 }) << "Prefix guard byte " << i;
            for (size_t i = alignment + count * sizeof(TerrainRuleResult); i < output.size; i++)
                ASSERT_EQ(bytes[i], std::byte{ 0xa5 }) << "Over-dispatch/tail guard byte " << i;
            Append("queries.bin", queries.data(), queries.size_bytes());
            Append("gpu-results.bin", bytes + alignment, count * sizeof(TerrainRuleResult));
            std::vector<RuleContract> expectedContracts, actualContracts;
            expectedContracts.reserve(count); actualContracts.reserve(count);
            for (size_t i = 0; i < queries.size(); i++)
            {
                TerrainRuleResult actual{};
                std::memcpy(&actual, bytes + alignment + i * sizeof(actual), sizeof(actual));
                const auto expected = Expected(queries[i]);
                const auto contract = Contract(actual);
                expectedContracts.push_back(expected);
                actualContracts.push_back(contract);
                EXPECT_EQ(contract, expected) << "GPU query " << queryCount + i;
            }
            Append("expected-contract.bin", expectedContracts.data(), expectedContracts.size() * sizeof(RuleContract));
            Append("actual-contract.bin", actualContracts.data(), actualContracts.size() * sizeof(RuleContract));
            queryCount += count;
            dispatchCount++;
        }
    };
}

TEST_F(VulkanTerrainSurfaceRulesTest, ActualDeviceEmissionsMatchFrozenNeighboursAtEveryRotationAndSlope)
{
    std::vector<TerrainRuleQuery> batch;
    batch.reserve(2048);
    uint64_t cases = 0;
    for (int rotation = 0; rotation < 4; rotation++)
    for (int base : { 16, 32, 48, 64 })
    for (int neighbourBase : { 16, 32, 48, 64 })
    for (int slope = 0; slope < 16; slope++)
    for (int neighbourSlope = 0; neighbourSlope < 16; neighbourSlope++)
    for (int edge = 0; edge < 4; edge++)
    {
        TerrainRuleQuery q{ { base, slope, rotation, slope & 7 }, { neighbourBase, neighbourSlope, 1, neighbourSlope % 3 - 1 },
                            { edge, 0, slope & 3, neighbourSlope & 3 } };
        const auto expected = Expected(q);
        const int emissions = std::max(1, expected[0]);
        for (int index = 0; index < emissions; index++)
        {
            q.query[1] = index;
            batch.push_back(q);
            if (batch.size() == 2048)
            {
                Dispatch(batch);
                ASSERT_FALSE(HasFatalFailure());
                batch.clear();
            }
        }
        cases++;
    }
    if (!batch.empty())
        Dispatch(batch);
    EXPECT_EQ(cases, 65536u);
    RecordProperty("terrainNeighbourCases", std::to_string(cases));
}

TEST_F(VulkanTerrainSurfaceRulesTest, InvalidMissingMaximumAndDispatchBoundaryQueriesPreserveGuards)
{
    for (size_t count : { 0u, 1u, 63u, 64u, 65u, 127u, 128u, 129u, 2048u })
    {
        std::vector<TerrainRuleQuery> queries(count, { { 64, 7, 2, 6 }, { 16, 0, 1, 1 }, { 0, 0, 3, 2 } });
        Dispatch(queries);
        ASSERT_FALSE(HasFatalFailure());
    }
    std::vector<TerrainRuleQuery> batch;
    const auto add = [&](TerrainRuleQuery q) {
        batch.push_back(q);
        if (batch.size() == 2048)
        {
            Dispatch(batch);
            batch.clear();
        }
    };
    for (int rotation = 0; rotation < 4; rotation++)
    for (int slope = 0; slope < 16; slope++)
    for (int edge = 0; edge < 4; edge++)
    {
        TerrainRuleQuery q{ { 4064, slope, rotation, 7 }, { 0, 0, 0, 2 }, { edge, 0, 1, 0 } };
        const int count = Expected(q)[0];
        for (int index = -1; index <= count; index++)
        {
            q.query[1] = index;
            add(q);
            ASSERT_FALSE(HasFatalFailure());
        }
    }
    const TerrainRuleQuery valid{ { 64, 0, 0, 0 }, { 16, 0, 1, 0 }, { 0, 0, 0, 0 } };
    for (int z : { -16, 0, 17, 4080 }) { auto q = valid; q.own[0] = z; add(q); }
    for (int slope : { -1, 16, 31 }) { auto q = valid; q.own[1] = slope; add(q); }
    for (int rotation : { -1, 4 }) { auto q = valid; q.own[2] = rotation; add(q); }
    for (int edge : { -1, 4 }) { auto q = valid; q.query[0] = edge; add(q); }
    for (int index : { -1, 256 }) { auto q = valid; q.query[1] = index; add(q); }
    { auto q = valid; q.neighbour[0] = 17; add(q); }
    { auto q = valid; q.neighbour[1] = 16; add(q); }
    if (!batch.empty()) Dispatch(batch);
}
#endif
