#include "VehicleSounds.h"

#include "../interface/Viewport.h"
#include "../interface/Window.h"
#include "../windows/Windows.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/Diagnostic.h>
#include <openrct2/audio/Audio.h>
#include <openrct2/audio/AudioChannel.h>
#include <openrct2/audio/AudioMixer.h>
#include <openrct2/audio/SpatialAudio.h>
#include <openrct2/entity/EntityRegistry.h>
#include <openrct2/profiling/Profiling.h>
#include <openrct2/ride/TrainManager.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OpenRCT2::Audio
{
    namespace
    {
        template<typename T>
        class TrainIterator;
        template<typename T>
        class Train
        {
        public:
            explicit Train(T* vehicle)
                : FirstCar(vehicle)
            {
                assert(FirstCar->IsHead());
            }
            int32_t GetMass() const;

            friend class TrainIterator<T>;
            using iterator = TrainIterator<T>;
            iterator begin() const
            {
                return iterator{ FirstCar };
            }
            iterator end() const
            {
                return iterator{};
            }

        private:
            T* FirstCar;
        };
        template<typename T>
        class TrainIterator
        {
        public:
            using iterator = TrainIterator;
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;
            using pointer = T*;
            using reference = T&;

            TrainIterator() = default;
            explicit TrainIterator(T* vehicle)
                : Current(vehicle)
            {
            }
            reference operator*() const
            {
                return *Current;
            }
            iterator& operator++()
            {
                Current = getGameState().entities.GetEntity<Vehicle>(NextVehicleId);
                if (Current != nullptr)
                {
                    NextVehicleId = Current->next_vehicle_on_train;
                }
                return *this;
            }
            iterator operator++(int)
            {
                iterator temp = *this;
                ++*this;
                return temp;
            }
            bool operator!=(const iterator& other) const
            {
                return Current != other.Current;
            }

        private:
            T* Current = nullptr;
            EntityId NextVehicleId = EntityId::GetNull();
        };
    } // namespace

    template<typename T>
    int32_t Train<T>::GetMass() const
    {
        return std::accumulate(
            begin(), end(), 0, [](int32_t totalMass, const Vehicle& vehicle) { return totalMass + vehicle.mass; });
    }

    static std::unordered_map<uint16_t, DopplerMotionState> _vehicleDopplerStates;
    static std::chrono::steady_clock::time_point _lastVehicleAudioUpdate{};
    static std::chrono::steady_clock::time_point _lastVehicleAudioReport{};
    static uint64_t _vehicleChannelStartAttempts{};
    static uint64_t _vehicleChannelStartFailures{};

    static bool SoundCanPlay(const Vehicle& vehicle, const SpatialAudioParams& spatial)
    {
        if (gLegacyScene == LegacyScene::scenarioEditor)
            return false;

        if (gLegacyScene == LegacyScene::trackDesigner && getGameState().editorStep != Editor::Step::rollerCoasterDesigner)
            return false;

        if (vehicle.sound1_id == SoundId::null && vehicle.sound2_id == SoundId::null)
            return false;

        if (vehicle.x == kLocationNull)
            return false;

        return spatial.Audible;
    }

    /**
     *
     *  rct2: 0x006BC2F3
     */
    static int32_t GetSoundPriority(const Vehicle& vehicle, const SpatialAudioParams& spatial, bool alreadyPlaying)
    {
        const auto sourceLevel = std::max(vehicle.sound1_volume, vehicle.sound2_volume) / 255.0f;
        const auto massWeight = 1.0f + std::min(1.0f, std::log2(1.0f + Train(&vehicle).GetMass()) / 16.0f);
        const auto motionWeight = 1.0f + std::min(0.5f, static_cast<float>(std::abs(vehicle.velocity)) / 400000.0f);
        int32_t result = static_cast<int32_t>(
            std::lround(spatial.Gain * std::max(0.1f, sourceLevel) * massWeight * motionWeight * 100000.0f));

        if (alreadyPlaying)
        {
            // A small continuity bonus avoids slot churn between similarly loud boundary candidates.
            result += 1000;
        }
        return result;
    }

    static CoordsXYZ GetClosestTrainSoundPosition(const Vehicle& head, const SpatialAudioListener& listener)
    {
        auto closestPosition = CoordsXYZ{ head.x, head.y, head.z };
        auto closestDistanceSquared = std::numeric_limits<float>::max();
        size_t visitedCars = 0;
        for (auto* car = &head; car != nullptr && visitedCars < 256;
             car = getGameState().entities.GetEntity<Vehicle>(car->next_vehicle_on_train), visitedCars++)
        {
            if (car->x == kLocationNull)
            {
                continue;
            }
            const auto dx = static_cast<float>(car->x - listener.Position.x);
            const auto dy = static_cast<float>(car->y - listener.Position.y);
            const auto dz = static_cast<float>(car->z - listener.Position.z);
            const auto distanceSquared = (dx * dx) + (dy * dy) + (dz * dz);
            if (distanceSquared < closestDistanceSquared)
            {
                closestDistanceSquared = distanceSquared;
                closestPosition = { car->x, car->y, car->z };
            }
        }
        return closestPosition;
    }

    static VehicleSoundParams CreateSoundParam(
        const Vehicle& vehicle, int32_t priority, const SpatialAudioParams& spatial, float elapsedSeconds)
    {
        VehicleSoundParams param;
        param.priority = priority;
        param.spatialGain = spatial.Gain;
        param.azimuth = spatial.Azimuth;
        param.elevation = spatial.Elevation;
        param.dopplerFactor = UpdateDopplerMotion(
            _vehicleDopplerStates[vehicle.id.ToUnderlying()], spatial.Distance, elapsedSeconds);

        int32_t frequency = std::abs(vehicle.velocity);

        const auto* rideType = vehicle.GetRideEntry();
        if (rideType != nullptr)
        {
            if (rideType->Cars[vehicle.vehicle_type].double_sound_frequency & 1)
            {
                frequency *= 2;
            }
        }

        // * 0.0105133...
        frequency >>= 5; // /32
        frequency *= 5512;
        frequency >>= 14; // /16384

        frequency += 11025;
        param.frequency = static_cast<uint16_t>(frequency);
        param.id = vehicle.id.ToUnderlying();
        return param;
    }

    /**
     *
     *  rct2: 0x006BB9FF
     */
    static void UpdateSoundParams(
        const Vehicle& vehicle, const SpatialAudioListener& listener, float elapsedSeconds, bool alreadyPlaying,
        std::vector<VehicleSoundParams>& vehicleSoundParamsList)
    {
        const auto sourcePosition = GetClosestTrainSoundPosition(vehicle, listener);
        auto occlusion = 1.0f;
        auto surfaceElement = MapGetSurfaceElementAt(CoordsXY{ sourcePosition.x, sourcePosition.y });
        if (surfaceElement != nullptr && surfaceElement->getBaseZ() > sourcePosition.z)
        {
            occlusion = 0.25f;
        }
        const auto spatial = CalculateSpatialAudioParams(
            listener, sourcePosition, occlusion, SpatialAudioRolloff::vehicle);
        if (!SoundCanPlay(vehicle, spatial))
            return;

        const auto soundPriority = GetSoundPriority(vehicle, spatial, alreadyPlaying);
        vehicleSoundParamsList.push_back(CreateSoundParam(vehicle, soundPriority, spatial, elapsedSeconds));
    }

    static void VehicleSoundsUpdateWindowSetup()
    {
        gMusicTrackingViewport = nullptr;

        WindowBase* window = Ui::Windows::WindowGetListening();
        if (window == nullptr)
        {
            return;
        }

        Viewport* viewport = WindowGetViewport(window);
        if (viewport == nullptr)
        {
            return;
        }

        gMusicTrackingViewport = viewport;
        gWindowAudioExclusive = window;
    }

    static bool IsLoopingSound(SoundId id)
    {
        switch (id)
        {
            case SoundId::liftClassic:
            case SoundId::trackFrictionClassicWood:
            case SoundId::frictionClassic:
            case SoundId::liftFrictionWheels:
            case SoundId::goKartEngine:
            case SoundId::trackFrictionTrain:
            case SoundId::trackFrictionWater:
            case SoundId::liftArrow:
            case SoundId::liftWood:
            case SoundId::trackFrictionWood:
            case SoundId::liftWildMouse:
            case SoundId::liftBM:
            case SoundId::trackFrictionBM:
            case SoundId::liftRMC:
            case SoundId::trackFrictionRMC:
            case SoundId::liftFlume:
                return true;
            default:
                return false;
        }
    }

    static bool IsRiderScreamSound(SoundId id)
    {
        switch (id)
        {
            case SoundId::scream1:
            case SoundId::scream2:
            case SoundId::scream3:
            case SoundId::scream4:
            case SoundId::scream5:
            case SoundId::scream6:
            case SoundId::scream7:
            case SoundId::scream8:
                return true;
            default:
                return false;
        }
    }

    static bool IsFixedFrequencySound(SoundId id)
    {
        if (IsRiderScreamSound(id))
        {
            return true;
        }
        switch (id)
        {
            case SoundId::trainWhistle:
            case SoundId::trainDeparting:
            case SoundId::tram:
                return true;
            default:
                return false;
        }
    }

    static bool IsSpecialFrequencySound(SoundId id)
    {
        switch (id)
        {
            case SoundId::trackFrictionBM:
            case SoundId::trackFrictionRMC:
                return true;
            default:
                return false;
        }
    }

    enum class SoundType
    {
        TrackNoises,
        OtherNoises, // e.g. Screams
    };

    template<SoundType type>
    static uint16_t SoundFrequency(const SoundId id, uint16_t baseFrequency)
    {
        if constexpr (type == SoundType::TrackNoises)
        {
            if (IsSpecialFrequencySound(id))
            {
                return (baseFrequency / 2) + 4000;
            }
            return baseFrequency;
        }
        else
        {
            if (IsFixedFrequencySound(id))
            {
                return 22050;
            }
            return std::min((baseFrequency * 2) - 3248, 25700);
        }
    }

    template<SoundType type>
    static void UpdateSound(const SoundId id, int32_t volume, VehicleSoundParams* sound_params, Sound& sound)
    {
        // Vehicle volume is an authored linear-amplitude byte, not a DirectSound decibel value.
        // Preserve that meaning before applying distance; the legacy arithmetic made 128 about -41 dB.
        auto authoredGain = std::clamp(static_cast<float>(volume) / 255.0f, 0.0f, 1.0f);
        if constexpr (type == SoundType::TrackNoises)
        {
            // The authored vehicle byte is conservative and the source is spread over a whole train.
            // A 4.5x calibration keeps wheel, lift, and engine texture present without dominating
            // the close view; the vehicle-specific rolloff then confines it more tightly in space.
            authoredGain *= 4.5f;
        }
        else
        {
            // Screams remain prominent above the diffuse crowd bed but are trimmed slightly from
            // the previous pass. Other secondary vehicle cues receive a smaller calibration.
            authoredGain *= IsRiderScreamSound(id) ? 3.5f : 1.75f;
        }
        volume = SpatialGainToDSEnvelope(authoredGain * sound_params->spatialGain);

        if (sound.channel != nullptr && sound.channel->IsDone() && IsLoopingSound(sound.id))
        {
            sound.id = SoundId::null;
            sound.channel = nullptr;
        }
        if (id != sound.id && sound.id != SoundId::null)
        {
            sound.id = SoundId::null;
            sound.channel->Stop();
        }
        if (id == SoundId::null)
        {
            return;
        }

        if (sound.id == SoundId::null)
        {
            auto frequency = SoundFrequency<type>(id, sound_params->frequency);
            auto looping = IsLoopingSound(id);
            _vehicleChannelStartAttempts++;
            auto channel = CreateAudioChannel(
                id, MixerGroup::Vehicle, looping, DStoMixerVolume(volume), 0.5f,
                DStoMixerRate(frequency) * sound_params->dopplerFactor, false);
            if (channel != nullptr)
            {
                sound.id = id;
                sound.volume = volume;
                sound.frequency = sound_params->frequency;
                sound.channel = channel;
                sound.channel->SetSpatial(sound_params->azimuth, sound_params->elevation);
            }
            else
            {
                _vehicleChannelStartFailures++;
                sound.id = SoundId::null;
            }
            return;
        }
        if (volume != sound.volume)
        {
            sound.volume = volume;
            sound.channel->SetVolume(DStoMixerVolume(volume));
        }
        sound.channel->SetSpatial(sound_params->azimuth, sound_params->elevation);
        if (!(getGameState().currentTicks & 3) && sound_params->frequency != sound.frequency)
        {
            sound.frequency = sound_params->frequency;
        }
        const auto frequency = SoundFrequency<type>(id, sound.frequency);
        sound.channel->SetRate(DStoMixerRate(frequency) * sound_params->dopplerFactor);
    }

    /**
     *
     *  rct2: 0x006BBC6B
     */
    void UpdateVehicleSounds()
    {
        PROFILED_FUNCTION();

        if (!IsAvailable())
            return;

        std::vector<VehicleSoundParams> vehicleSoundParamsList;
        vehicleSoundParamsList.reserve(kMaxVehicleSounds * 2);

        VehicleSoundsUpdateWindowSetup();
        const auto listener = GetSpatialAudioListener();
        if (!listener.has_value())
            return;
        UpdateSpatialSounds();

        const auto now = std::chrono::steady_clock::now();
        const auto elapsedSeconds = _lastVehicleAudioUpdate == std::chrono::steady_clock::time_point{}
            ? 0.0f
            : std::chrono::duration<float>(now - _lastVehicleAudioUpdate).count();
        _lastVehicleAudioUpdate = now;

        std::unordered_set<uint16_t> playingVehicleIds;
        playingVehicleIds.reserve(kMaxVehicleSounds);
        for (const auto& vehicleSound : gVehicleSoundList)
        {
            if (vehicleSound.id != kSoundIdNull)
            {
                playingVehicleIds.insert(vehicleSound.id);
            }
        }

        for (auto vehicle : TrainManager::View())
        {
            UpdateSoundParams(
                *vehicle, *listener, elapsedSeconds, playingVehicleIds.contains(vehicle->id.ToUnderlying()),
                vehicleSoundParamsList);
        }
        std::stable_sort(vehicleSoundParamsList.begin(), vehicleSoundParamsList.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.priority > rhs.priority;
        });
        const auto candidateCount = vehicleSoundParamsList.size();
        if (vehicleSoundParamsList.size() > kMaxVehicleSounds)
        {
            vehicleSoundParamsList.resize(kMaxVehicleSounds);
        }
        const auto shouldReport = _lastVehicleAudioReport == std::chrono::steady_clock::time_point{}
            || now - _lastVehicleAudioReport >= std::chrono::seconds(5);

        std::unordered_set<uint16_t> selectedVehicleIds;
        selectedVehicleIds.reserve(vehicleSoundParamsList.size());
        for (const auto& params : vehicleSoundParamsList)
        {
            selectedVehicleIds.insert(params.id);
        }

        // Stop all playing sounds that no longer have priority. The selected-id set keeps this linear
        // when thousands of vehicle slots are active.
        for (auto& vehicleSound : gVehicleSoundList)
        {
            if (vehicleSound.id != kSoundIdNull && !selectedVehicleIds.contains(vehicleSound.id))
            {
                if (vehicleSound.trackSound.id != SoundId::null)
                {
                    vehicleSound.trackSound.channel->Stop();
                }
                if (vehicleSound.otherSound.id != SoundId::null)
                {
                    vehicleSound.otherSound.channel->Stop();
                }
                vehicleSound.id = kSoundIdNull;
            }
        }

        std::unordered_map<uint16_t, VehicleSound*> activeVehicleSounds;
        std::vector<VehicleSound*> freeVehicleSounds;
        activeVehicleSounds.reserve(vehicleSoundParamsList.size());
        freeVehicleSounds.reserve(kMaxVehicleSounds);
        for (auto& vehicleSound : gVehicleSoundList)
        {
            if (vehicleSound.id == kSoundIdNull)
            {
                freeVehicleSounds.push_back(&vehicleSound);
            }
            else
            {
                activeVehicleSounds.emplace(vehicleSound.id, &vehicleSound);
            }
        }

        for (auto& vehicleSoundParams : vehicleSoundParamsList)
        {
            VehicleSound* vehicleSound{};
            const auto active = activeVehicleSounds.find(vehicleSoundParams.id);
            if (active != activeVehicleSounds.end())
            {
                vehicleSound = active->second;
            }
            else if (!freeVehicleSounds.empty())
            {
                vehicleSound = freeVehicleSounds.back();
                freeVehicleSounds.pop_back();
                vehicleSound->id = vehicleSoundParams.id;
                vehicleSound->trackSound.id = SoundId::null;
                vehicleSound->otherSound.id = SoundId::null;
                vehicleSound->volume = 0x30;
                activeVehicleSounds.emplace(vehicleSoundParams.id, vehicleSound);
            }
            if (vehicleSound == nullptr)
            {
                continue;
            }

            Vehicle* vehicle = getGameState().entities.GetEntity<Vehicle>(EntityId::FromUnderlying(vehicleSoundParams.id));
            if (vehicle != nullptr)
            {
                UpdateSound<SoundType::TrackNoises>(
                    vehicle->sound1_id, vehicle->sound1_volume, &vehicleSoundParams, vehicleSound->trackSound);
                UpdateSound<SoundType::OtherNoises>(
                    vehicle->sound2_id, vehicle->sound2_volume, &vehicleSoundParams, vehicleSound->otherSound);
            }
        }

        if (shouldReport)
        {
            size_t trackChannels = 0;
            size_t secondaryChannels = 0;
            for (const auto& vehicleSound : gVehicleSoundList)
            {
                trackChannels += vehicleSound.id != kSoundIdNull && vehicleSound.trackSound.id != SoundId::null;
                secondaryChannels += vehicleSound.id != kSoundIdNull && vehicleSound.otherSound.id != SoundId::null;
            }
            LOG_VERBOSE(
                "Spatial audio selected %zu vehicle emitters from %zu continuous-distance candidates (%zu mechanical, "
                "%zu secondary channels); starts %llu, failures %llu",
                vehicleSoundParamsList.size(), candidateCount, trackChannels, secondaryChannels,
                static_cast<unsigned long long>(_vehicleChannelStartAttempts),
                static_cast<unsigned long long>(_vehicleChannelStartFailures));
            _vehicleChannelStartAttempts = 0;
            _vehicleChannelStartFailures = 0;
            _lastVehicleAudioReport = now;
        }
    }
} // namespace OpenRCT2::Audio
