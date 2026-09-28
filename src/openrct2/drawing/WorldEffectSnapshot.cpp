// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include "WorldEffectSnapshot.h"

#include "../GameState.h"
#include "../entity/Duck.h"
#include "../entity/EntityTweener.h"
#include "../entity/JumpingFountain.h"
#include "../entity/Litter.h"
#include "../entity/Particle.h"
#include "../profiling/Profiling.h"
namespace OpenRCT2::Drawing
{
    std::shared_ptr<const WorldEffectSnapshot> CaptureWorldEffects(uint32_t sourceTick)
    {
        PROFILED_FUNCTION();
        auto result = std::make_shared<WorldEffectSnapshot>();
        auto& entities = getGameState().entities;
        result->epoch = entities.GetEntityVisualEpoch();
        result->sourceTick = sourceTick;
        for (auto type :
             { EntityType::litter, EntityType::steamParticle, EntityType::crashedVehicleParticle, EntityType::explosionCloud,
               EntityType::crashSplash, EntityType::explosionFlare, EntityType::jumpingFountain, EntityType::duck })
            for (const auto* entity : entities.GetEntityExecutionList(type))
            {
                // Capture the completed simulation position even if a UI render has
                // temporarily interpolated the live entity between tick endpoints.
                auto position = entity->getLocation();
                if (const auto motion = EntityTweener::get().GetMotion(*entity))
                    position = motion->current;
                WorldEffectRecord record{ position.x, position.y, position.z, static_cast<uint32_t>(type),
                                          entity->orientation };
                switch (type)
                {
                    case EntityType::litter:
                        record.state = static_cast<uint32_t>(static_cast<const Litter*>(entity)->subType);
                        break;
                    case EntityType::steamParticle:
                        record.frame = static_cast<const SteamParticle*>(entity)->frame;
                        break;
                    case EntityType::crashedVehicleParticle:
                    {
                        auto& p = *static_cast<const VehicleCrashParticle*>(entity);
                        record.frame = p.frame;
                        record.state = p.crashedSpriteBase;
                        record.colours = static_cast<uint32_t>(p.colour[0]) | (static_cast<uint32_t>(p.colour[1]) << 8);
                        break;
                    }
                    case EntityType::explosionCloud:
                        record.frame = static_cast<const ExplosionCloud*>(entity)->frame;
                        break;
                    case EntityType::crashSplash:
                        record.frame = static_cast<const CrashSplashParticle*>(entity)->frame;
                        break;
                    case EntityType::explosionFlare:
                        record.frame = static_cast<const ExplosionFlare*>(entity)->frame;
                        break;
                    case EntityType::jumpingFountain:
                    {
                        auto& p = *static_cast<const JumpingFountain*>(entity);
                        record.frame = p.frame;
                        record.state = static_cast<uint32_t>(p.fountainType);
                        break;
                    }
                    case EntityType::duck:
                    {
                        auto& p = *static_cast<const Duck*>(entity);
                        record.frame = p.frame;
                        record.state = static_cast<uint32_t>(p.state);
                        break;
                    }
                    default:
                        break;
                }
                result->records.push_back(record);
            }
        return result;
    }
} // namespace OpenRCT2::Drawing
