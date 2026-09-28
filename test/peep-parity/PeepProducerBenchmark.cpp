/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
// Explicit standalone CPU diagnostic. No test registration, simulation advancement or renderer acquisition.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "PeepCaptureFixture.h"
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/command_line/CommandLine.hpp>
#include <openrct2/config/Config.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/drawing/Colour.h>
#include <openrct2/drawing/RetainedPeepState.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/entity/Staff.h>
#include <openrct2/world/Map.h>

namespace
{
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;
    using Clock = std::chrono::steady_clock;
    constexpr uint32_t kPopulation = 60000;
    constexpr uint32_t kTick = 1234;
    const std::array<uint32_t, 4> kGenerations{ 7, 11, 13, 17 };
    void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    double Nanoseconds(Clock::time_point start) { return std::chrono::duration<double, std::nano>(Clock::now() - start).count(); }

    struct RawPublication
    {
        std::vector<RetainedPeepRecord> records;
        std::vector<EntityVisualChange> balloons;
    };

    // Reproduce the prior raw96 producer and its non-balloon metadata fan-out. The input IDs are the same
    // coalesced worklist queued for Registry below. Neither producer performs dirty marking or acknowledgement here.
    RawPublication CaptureRaw96(EntityRegistry& registry, std::span<const EntityId> worklist)
    {
        RawPublication result;
        result.records.reserve(worklist.size());
        result.balloons.reserve(worklist.size());
        for (const auto id : worklist)
        {
            const auto* entity = registry.tryGetEntity(id);
            const auto& peep = *entity->cast<Peep>();
            result.records.push_back(CaptureRetainedPeepRecord(
                peep, registry.GetEntityVisualHandle(id), kGenerations.at(peep.animationObjectIndex), kTick));
            EntityVisualChange change{};
            change.handle = registry.GetEntityVisualHandle(id);
            change.type = entity->type;
            change.present = true;
            change.dirty = EntityVisualDirty::full;
            result.balloons.push_back(change);
        }
        return result;
    }

    template<typename T> size_t Bytes(const std::vector<T>& values) { return values.size() * sizeof(T); }
    template<typename T> size_t CapacityBytes(const std::vector<T>& values) { return values.capacity() * sizeof(T); }
    template<typename T> bool SameBytes(const std::vector<T>& left, const std::vector<T>& right)
    {
        return left.size() == right.size() && (left.empty() || std::memcmp(left.data(), right.data(), Bytes(left)) == 0);
    }
    uint64_t Hash(std::span<const std::byte> bytes)
    {
        uint64_t result = 1469598103934665603ull;
        while (bytes.size() >= sizeof(uint64_t))
        {
            uint64_t word;
            std::memcpy(&word, bytes.data(), sizeof(word));
            result = (result ^ word) * 1099511628211ull;
            bytes = bytes.subspan(sizeof(word));
        }
        for (const auto value : bytes)
            result = (result ^ std::to_integer<uint8_t>(value)) * 1099511628211ull;
        return result;
    }
    template<typename T> uint64_t Hash(const std::vector<T>& values) { return Hash(std::as_bytes(std::span(values))); }
    void VerifyBalloons(const std::vector<EntityVisualChange>& changes, const RawPublication& raw)
    {
        Require(changes.size() == raw.balloons.size(), "Balloon metadata fan-out count differs");
        // Compare semantics explicitly: EntityVisualChange has padding that is not part of the contract.
        for (size_t i = 0; i < changes.size(); ++i)
        {
            const auto& a = changes[i];
            const auto& b = raw.balloons[i];
            Require(a.handle == b.handle && a.type == b.type && a.present == b.present && a.dirty == b.dirty
                && a.payloadOffset == 0 && a.payloadSize == 0 && a.orientation == 0
                && a.location == CoordsXYZ{} && a.spriteData.width == 0 && a.spriteData.heightMin == 0
                && a.spriteData.heightMax == 0, "Balloon metadata fan-out content differs");
        }
    }
    RetainedPeepBatch ExpectedGroups(const RawPublication& raw, EntityVisualDirty mask)
    {
        RetainedPeepBatch result;
        const auto has = [mask](EntityVisualDirty flag) { return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(flag)) != 0; };
        const bool all = has(EntityVisualDirty::presence);
        for (const auto& record : raw.records)
        {
            const auto f = SplitRetainedPeepRecord(record);
            if (all) result.lifecycle.push_back({ f.id, f.lifecycle });
            if (all || has(EntityVisualDirty::transform)) result.motion.push_back({ f.id, record.generation, f.motion });
            if (all || has(EntityVisualDirty::appearance)) result.appearance.push_back({ f.id, record.generation, f.appearance });
            if (all || has(EntityVisualDirty::animation | EntityVisualDirty::bounds))
                result.animation.push_back({ f.id, record.generation, f.animation });
        }
        return result;
    }
    void VerifyGroups(const RetainedEntityPublicationInput& input, const RetainedPeepBatch& expected, const RawPublication& raw)
    {
        Require(!input.bootstrap && !input.peeps.reset && input.bootstrapVisits == 0 && input.dirtyVisits == kPopulation,
            "Capture traversed a bootstrap or unexpected worklist");
        Require(SameBytes(input.peeps.lifecycle, expected.lifecycle) && SameBytes(input.peeps.motion, expected.motion)
            && SameBytes(input.peeps.appearance, expected.appearance) && SameBytes(input.peeps.animation, expected.animation),
            "Direct group bytes differ from raw96 projection");
        Require(input.balloons.payload.empty(), "Unexpected non-peep concrete payload");
        VerifyBalloons(input.balloons.changes, raw);
    }
    json_t Layout(const RetainedEntityPublicationInput& input)
    {
        json_t result;
        size_t payload = 0, capacity = 0;
        const auto add = [&](const char* name, const auto& values) {
            result[name] = { { "count", values.size() }, { "payloadBytes", Bytes(values) },
                { "capacityBytes", CapacityBytes(values) } };
            payload += Bytes(values);
            capacity += CapacityBytes(values);
        };
        add("lifecycle", input.peeps.lifecycle);
        add("motion", input.peeps.motion);
        add("appearance", input.peeps.appearance);
        add("animation", input.peeps.animation);
        result["peepPayloadBytes"] = payload;
        result["peepCapacityBytes"] = capacity;
        result["peepPayloadBytesPerEntity"] = payload / kPopulation;
        add("balloonMetadata", input.balloons.changes);
        add("balloonConcretePayload", input.balloons.payload);
        result["totalPayloadBytes"] = payload;
        result["totalCapacityBytes"] = capacity;
        return result;
    }
    json_t Statistics(std::vector<double> values)
    {
        const double total = std::accumulate(values.begin(), values.end(), 0.0);
        std::sort(values.begin(), values.end());
        return { { "samples", values.size() }, { "totalMilliseconds", total / 1e6 },
            { "meanMicroseconds", total / static_cast<double>(values.size()) / 1e3 },
            { "p50Microseconds", values[(values.size() - 1) / 2] / 1e3 },
            { "p95Microseconds", values[(values.size() - 1) * 95 / 100] / 1e3 } };
    }
    void VerifySnapshot(const RetainedPeepSnapshot& snapshot, const RawPublication& raw)
    {
        Require(snapshot.count == kPopulation, "Retained snapshot count differs");
        for (const auto& record : raw.records)
        {
            const auto actual = snapshot.TryGet(EntityId::FromUnderlying(static_cast<uint16_t>(record.id)));
            Require(actual && *actual == record, "Joined retained snapshot differs from raw96");
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        std::map<std::string, std::string> args;
        for (int i = 1; i < argc; i += 2)
        {
            Require(i + 1 < argc && std::string_view(argv[i]).starts_with("--"), "Expected --key value arguments");
            Require(args.emplace(std::string(argv[i]).substr(2), argv[i + 1]).second, "Repeated argument");
        }
        const auto iterations = std::stoul(args.contains("iterations") ? args.at("iterations") : "3000");
        const auto warmup = std::stoul(args.contains("warmup") ? args.at("warmup") : "50");
        Require(iterations > 0 && iterations <= 3000 && warmup <= 1000, "Iteration limits exceeded");
        Require(!std::filesystem::exists(args.at("output")), "Refusing to overwrite benchmark result");
        gCustomUserDataPath = args.at("profile");
        gCustomOpenRCT2DataPath = args.at("data");
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        Require(Config::SetDefaults(), "Configuration defaults failed");
        auto context = CreateContext();
        Require(context->Initialise(), "Nongraphical context initialization failed");
        MapInit({ 16, 16 });
        auto& registry = getGameState().entities;
        registry.resetAllEntities();
        std::vector<EntityId> worklist;
        std::vector<Peep*> heldLive;
        worklist.reserve(kPopulation);
        heldLive.reserve(kPopulation);
        for (uint32_t i = 0; i < kPopulation; ++i)
        {
            auto* peep = (i % 60 == 0) ? static_cast<Peep*>(registry.createEntity<Staff>())
                                      : static_cast<Peep*>(registry.createEntity<Guest>());
            Require(peep != nullptr, "Could not allocate the required 60000 peeps");
            peep->x = static_cast<int32_t>(32 + i % 384);
            peep->y = static_cast<int32_t>(32 + (i / 384) % 384);
            peep->z = static_cast<int32_t>((i % 16) * 8);
            peep->orientation = static_cast<uint8_t>(i % 32);
            peep->state = PeepState::walking;
            peep->animationObjectIndex = static_cast<ObjectEntryIndex>(i % kGenerations.size());
            peep->animationGroup = static_cast<PeepAnimationGroup>(i % 4);
            peep->animationType = PeepAnimationType::walking;
            peep->nextAnimationType = PeepAnimationType::walking;
            peep->action = static_cast<PeepActionType>(i % 8);
            peep->animationImageIdOffset = static_cast<uint8_t>(i % 16);
            peep->spriteData.width = 8;
            peep->spriteData.heightMin = 5;
            peep->spriteData.heightMax = 24;
            peep->setClothingColours(static_cast<Colour>(i % 32), static_cast<Colour>((i / 32) % 32));
            if (peep->type == EntityType::guest)
                static_cast<Guest*>(peep)->setAccessoryColours(static_cast<Colour>(i % 32),
                    static_cast<Colour>((i + 7) % 32), static_cast<Colour>((i + 13) % 32));
            else
                static_cast<Staff*>(peep)->assignedStaffType = static_cast<StaffType>((i / 60) % 4);
            worklist.push_back(peep->id);
            heldLive.push_back(peep);
        }
        const auto initialRaw = CaptureRaw96(registry, worklist);
        RetainedPeepScene scene;
        auto bootstrap = registry.CaptureRetainedEntityPublication(kTick, kGenerations, true);
        Require(scene.Apply(bootstrap.peeps, 1), "Initial retained scene did not publish");
        const auto heldSnapshot = scene.GetSnapshot();
        VerifySnapshot(*heldSnapshot, initialRaw);
        registry.AcknowledgeRetainedEntityPublication();
        bootstrap = {};
        struct Workload { const char* name; EntityVisualDirty dirty; };
        const std::array workloads{
            Workload{ "movement-only", EntityVisualDirty::transform },
            Workload{ "appearance-only", EntityVisualDirty::appearance },
            Workload{ "animation-only", EntityVisualDirty::animation | EntityVisualDirty::bounds },
            Workload{ "motion-and-animation", EntityVisualDirty::transform | EntityVisualDirty::animation | EntityVisualDirty::bounds },
            Workload{ "all-groups-including-lifecycle", EntityVisualDirty::full },
        };
        json_t cases = json_t::array();
        uint64_t sequence = 1;
        for (const auto& workload : workloads)
        {
            // Setup only. No simulation, real mutation path or timer is being benchmarked by these notifications.
            for (auto* peep : heldLive)
                registry.PublishEntityVisualState(*peep, workload.dirty);
            const auto expectedRaw = CaptureRaw96(registry, worklist);
            const auto expectedGroups = ExpectedGroups(expectedRaw, workload.dirty);
            auto initial = registry.CaptureRetainedEntityPublication(kTick, kGenerations, false);
            VerifyGroups(initial, expectedGroups, expectedRaw);
            const auto layout = Layout(initial);
            initial = {};
            std::vector<double> rawTimes, directTimes, rawDestroyTimes, directDestroyTimes;
            for (auto* values : { &rawTimes, &directTimes, &rawDestroyTimes, &directDestroyTimes }) values->reserve(iterations);
            double verificationNanoseconds = 0;
            for (size_t iteration = 0; iteration < iterations + warmup; ++iteration)
            {
                const auto raw = [&] {
                    auto start = Clock::now();
                    auto value = CaptureRaw96(registry, worklist);
                    const auto elapsed = Nanoseconds(start);
                    start = Clock::now();
                    Require(SameBytes(value.records, expectedRaw.records), "Raw96 benchmark output changed");
                    VerifyBalloons(value.balloons, expectedRaw);
                    verificationNanoseconds += Nanoseconds(start);
                    start = Clock::now();
                    value = {};
                    const auto destroy = Nanoseconds(start);
                    if (iteration >= warmup) { rawTimes.push_back(elapsed); rawDestroyTimes.push_back(destroy); }
                };
                const auto direct = [&] {
                    auto start = Clock::now();
                    auto value = registry.CaptureRetainedEntityPublication(kTick, kGenerations, false);
                    const auto elapsed = Nanoseconds(start);
                    start = Clock::now();
                    VerifyGroups(value, expectedGroups, expectedRaw);
                    verificationNanoseconds += Nanoseconds(start);
                    start = Clock::now();
                    value = {};
                    const auto destroy = Nanoseconds(start);
                    if (iteration >= warmup) { directTimes.push_back(elapsed); directDestroyTimes.push_back(destroy); }
                };
                // Counterbalance first/second cache and allocator effects in each paired iteration.
                if (iteration % 2 == 0) { raw(); direct(); } else { direct(); raw(); }
            }
            auto final = registry.CaptureRetainedEntityPublication(kTick, kGenerations, false);
            VerifyGroups(final, expectedGroups, expectedRaw);
            static_cast<void>(scene.Apply(final.peeps, ++sequence));
            VerifySnapshot(*scene.GetSnapshot(), expectedRaw);
            VerifySnapshot(*heldSnapshot, initialRaw);
            const auto currentRaw = CaptureRaw96(registry, worklist);
            Require(SameBytes(currentRaw.records, initialRaw.records), "Benchmark mutated authoritative peep facts");
            for (size_t i = 0; i < heldLive.size(); ++i)
                Require(registry.tryGetEntity(worklist[i]) == heldLive[i], "Held live peep identity changed");
            json_t hashes{ { "raw96", Hash(currentRaw.records) }, { "lifecycle", Hash(final.peeps.lifecycle) },
                { "motion", Hash(final.peeps.motion) }, { "appearance", Hash(final.peeps.appearance) },
                { "animation", Hash(final.peeps.animation) } };
            json_t expectedHashes{ { "raw96", Hash(expectedRaw.records) }, { "lifecycle", Hash(expectedGroups.lifecycle) },
                { "motion", Hash(expectedGroups.motion) }, { "appearance", Hash(expectedGroups.appearance) },
                { "animation", Hash(expectedGroups.animation) } };
            Require(hashes == expectedHashes, "Final diagnostic checksums differ from raw96 projection");
            cases.push_back({ { "name", workload.name }, { "dirtyMask", static_cast<uint8_t>(workload.dirty) },
                { "raw96Capture", Statistics(rawTimes) }, { "directRegistryCapture", Statistics(directTimes) },
                { "raw96Destroy", Statistics(rawDestroyTimes) }, { "directRegistryDestroy", Statistics(directDestroyTimes) },
                { "raw96PeepPayloadBytes", Bytes(expectedRaw.records) }, { "raw96PeepCapacityBytes", CapacityBytes(expectedRaw.records) },
                { "raw96TotalPayloadBytes", Bytes(expectedRaw.records) + Bytes(expectedRaw.balloons) },
                { "raw96TotalCapacityBytes", CapacityBytes(expectedRaw.records) + CapacityBytes(expectedRaw.balloons) },
                { "directLayout", layout }, { "checksums", hashes }, { "expectedChecksums", expectedHashes }, { "equivalent", true },
                { "heldSnapshotAndLiveFactsUnchanged", true }, { "everyIterationByteCompared", true },
                { "verificationMillisecondsIncludingWarmup", verificationNanoseconds / 1e6 } });
            registry.AcknowledgeRetainedEntityPublication();
        }
        json_t report{ { "schema", 1 }, { "status", "pass" }, { "population", kPopulation }, { "guests", 59000 },
            { "staff", 1000 }, { "iterations", iterations }, { "warmup", warmup }, { "sourceTick", kTick },
            { "cases", std::move(cases) }, { "graphicsDisabled", true }, { "rendererAcquired", false },
            { "scope", "CPU capture microbenchmark on one frozen 60000-peep dirty worklist; not simulation/TPS/rendering performance" },
            { "baseline", "Preserved CaptureRetainedPeepRecord raw96 plus matching non-balloon metadata fan-out; includes its raw scalar validation" },
            { "baselineImplementation", "Diagnostic inline raw capture helper through public registry lookup, compared with linked production Registry capture; not a historical binary replay" },
            { "timing", "Fresh vector allocation/capture timed; destruction separately timed; byte checks, setup, dirty marking, acknowledgement and scene Apply excluded" },
            { "assetGenerations", "Four fixed nonzero diagnostic generation tokens; no sprite/object assets or catalog evaluation" },
            { "capacitySampling", "Fresh initial capture per case, exact vector capacity rather than logical bytes; timing iterations also allocate fresh vectors" },
            { "cachePolicy", "Repeated unchanged live facts, paired paths alternate first position; no cold-cache or changing-population claim" } };
        std::ofstream output(args.at("output"));
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << report.dump(2) << '\n';
        output.close();
        std::cout << report.dump(2) << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Peep producer benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
