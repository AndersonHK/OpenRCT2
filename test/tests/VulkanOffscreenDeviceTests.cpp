/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>

#ifdef ENABLE_VULKAN
    #include <array>
    #include <cstdlib>
    #include <cstring>
    #include <memory>
    #include <openrct2-renderer/vulkan/VulkanSubmissionSlots.h>
    #include <stdexcept>
    #include <string>

namespace
{
    namespace Vulkan = OpenRCT2::Ui::Vulkan;
    class VulkanOffscreenDeviceTest : public testing::Test
    {
    protected:
        std::shared_ptr<Vulkan::DeviceContext> context;
        void SetUp() override
        {
            try
            {
                context = Vulkan::DeviceContext::CreateGraphicsOnly();
            }
            catch (const std::exception& error)
            {
                const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
                if (required != nullptr && std::string(required) == "1")
                    FAIL() << "Required surface-free Vulkan device failed: " << error.what();
                GTEST_SKIP() << "Surface-free Vulkan device unavailable: " << error.what();
            }
        }

        static Vulkan::UploadAllocation RecordFill(const Vulkan::SubmissionToken& token, uint32_t value)
        {
            auto allocation = token.upload->Allocate(4096, alignof(uint32_t));
            if (!allocation)
                throw std::runtime_error("Fixture upload allocation failed");
            vkCmdFillBuffer(token.commandBuffer, allocation.buffer, allocation.offset, allocation.size, value);
            const VkBufferMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = allocation.buffer,
                .offset = allocation.offset,
                .size = allocation.size,
            };
            vkCmdPipelineBarrier(
                token.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0,
                nullptr);
            return allocation;
        }

        static void ExpectFilled(
            const Vulkan::SubmissionToken& token, const Vulkan::UploadAllocation& allocation, uint32_t expected)
        {
            token.upload->Invalidate(allocation.offset, allocation.size);
            std::array<uint32_t, 1024> bytes{};
            std::memcpy(bytes.data(), allocation.data, allocation.size);
            for (const auto actual : bytes)
                ASSERT_EQ(actual, expected);
        }
    };
} // namespace

TEST_F(VulkanOffscreenDeviceTest, TwoDomainsShareOneDeviceAndRetainItThroughRealFenceCompletion)
{
    ASSERT_FALSE(context->SupportsPresentation());
    ASSERT_FALSE(context->GetQueueFamilies().present.has_value());
    const auto device = context->GetDevice();
    const auto physicalDevice = context->GetPhysicalDevice();
    std::weak_ptr<Vulkan::DeviceContext> retained = context;
    auto first = std::make_unique<Vulkan::SubmissionSlots>(context, 1024 * 1024);
    auto second = std::make_unique<Vulkan::SubmissionSlots>(context, 1024 * 1024);
    context.reset();
    ASSERT_EQ(first->GetContext()->GetDevice(), device);
    ASSERT_EQ(second->GetContext()->GetDevice(), device);
    ASSERT_EQ(first->GetContext()->GetPhysicalDevice(), physicalDevice);
    ASSERT_EQ(second->GetContext()->GetPhysicalDevice(), physicalDevice);
    auto a = first->Begin(0, true);
    auto b = second->Begin(0, true);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_NE(a->commandBuffer, b->commandBuffer);
    const auto firstBytes = RecordFill(*a, 0x12345678);
    const auto secondBytes = RecordFill(*b, 0x89abcdef);
    EXPECT_NE(firstBytes.buffer, secondBytes.buffer);
    EXPECT_THROW(second->Submit(*a), std::logic_error);
    first->Submit(*a);
    second->Submit(*b);
    // A zero timeout is allowed to complete on fast hardware. Either outcome
    // must leave this exact token/allocation valid until a real wait succeeds.
    static_cast<void>(first->Wait(*a, 0));
    ASSERT_TRUE(first->Wait(*a, UINT64_MAX));
    ASSERT_TRUE(second->Wait(*b, UINT64_MAX));
    ExpectFilled(*a, firstBytes, 0x12345678);
    ExpectFilled(*b, secondBytes, 0x89abcdef);
    first.reset();
    EXPECT_FALSE(retained.expired());
    auto next = second->Begin(1, true);
    ASSERT_TRUE(next.has_value());
    auto afterRelease = RecordFill(*next, 0xfedcba98);
    second->Submit(*next);
    ASSERT_TRUE(second->Wait(*next, UINT64_MAX));
    ExpectFilled(*next, afterRelease, 0xfedcba98);
    second.reset();
    EXPECT_TRUE(retained.expired());
}

TEST_F(VulkanOffscreenDeviceTest, ReuseRejectsStaleTokensAndAbandonDoesNotSubmitOrResetAnotherDomain)
{
    OpenRCT2::Drawing::RenderUploadTelemetry telemetry;
    Vulkan::SubmissionSlots slots(context, 1024 * 1024);
    auto original = slots.Begin(0, true, &telemetry);
    ASSERT_TRUE(original.has_value());
    EXPECT_THROW(slots.Begin(0, false), std::logic_error);
    slots.Abandon(*original);
    EXPECT_FALSE(telemetry.submitted);
    EXPECT_THROW(slots.Wait(*original, 0), std::logic_error);
    auto replacement = slots.Begin(0, false, &telemetry);
    ASSERT_TRUE(replacement.has_value());
    EXPECT_GT(replacement->generation, original->generation);
    EXPECT_THROW(slots.Submit(*original), std::logic_error);
    auto bytes = RecordFill(*replacement, 0x55aa55aa);
    slots.Submit(*replacement);
    EXPECT_TRUE(telemetry.submitted);
    EXPECT_THROW(slots.Submit(*replacement), std::logic_error);
    EXPECT_THROW(slots.Abandon(*replacement), std::logic_error);
    ASSERT_TRUE(slots.Wait(*replacement, UINT64_MAX));
    ExpectFilled(*replacement, bytes, 0x55aa55aa);
}

TEST(VulkanSubmissionSlotsContractTest, RejectsAbsentDeviceBeforeAnyGpuWork)
{
    EXPECT_THROW(Vulkan::SubmissionSlots(nullptr), std::invalid_argument);
}

TEST(VulkanOffscreenDeviceOwnerTest, RecreatedDomainsReuseOneOwnerAndRetainDeviceUntilFinalLease)
{
    auto owner = std::make_shared<Vulkan::DeviceContextOwner>(false);
    ASSERT_FALSE(owner->IsCreated());
    std::shared_ptr<Vulkan::DeviceContext> first;
    try
    {
        first = owner->AcquireOffscreen();
    }
    catch (const std::exception& error)
    {
        const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
        if (required != nullptr && std::string(required) == "1")
            FAIL() << "Required owned Vulkan device failed: " << error.what();
        GTEST_SKIP() << "Owned surface-free Vulkan device unavailable: " << error.what();
    }
    ASSERT_TRUE(owner->IsCreated());
    std::weak_ptr<Vulkan::DeviceContext> lifetime = first;
    const auto device = first->GetDevice();
    {
        Vulkan::SubmissionSlots retiredDomain(first, 1024 * 1024, 1);
        EXPECT_EQ(retiredDomain.GetContext(), first);
    }
    first.reset();
    EXPECT_FALSE(lifetime.expired());
    auto auxiliary = owner->AcquireOffscreen();
    auto replacement = std::make_unique<Vulkan::SubmissionSlots>(auxiliary, 1024 * 1024, 1);
    EXPECT_EQ(replacement->GetContext()->GetDevice(), device);
    EXPECT_EQ(auxiliary, owner->AcquireOffscreen());
    owner.reset();
    EXPECT_FALSE(lifetime.expired());
    auxiliary.reset();
    EXPECT_EQ(replacement->GetContext()->GetDevice(), device);
    replacement.reset();
    EXPECT_TRUE(lifetime.expired());
}
#endif
