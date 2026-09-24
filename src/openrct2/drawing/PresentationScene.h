/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "PresentationGeneration.h"

#include <cstdint>
#include <memory>

class JobPool;

namespace OpenRCT2
{
    class EntityRegistry;
    namespace Drawing
    {
        struct BalloonPublicationCopyTotals;
    }

    /**
     * Sole owner of main-window presentation publication.
     *
     * A caller begins at most one immutable generation per draw and captures/queues its replacement at that same owner
     * boundary, before window traversal. Map and entity snapshots share one source tick and never advance independently.
     * Native terrain uses the previous complete generation while preparation is pending, including during interactive
     * map edits. Only bootstrap/lifecycle recovery may wait; no worker reads live mutable map or entity state.
     */
    class PresentationScene final
    {
    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;

    public:
        PresentationScene();
        ~PresentationScene();

        PresentationScene(const PresentationScene&) = delete;
        PresentationScene& operator=(const PresentationScene&) = delete;

        /** Returns true when a new generation was admitted for this draw. */
        bool BeginFrame(
            JobPool& jobs, EntityRegistry& entities, uint32_t drawCount, bool synchronousMapPublication,
            EntityPublicationProfile profile = EntityPublicationProfile::legacyBulk,
            std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> peepAnimations = {});
        void ScheduleNext(
            JobPool& jobs, EntityRegistry& entities,
            std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> peepAnimations = {});
        void Reset(JobPool& jobs);
        // Producer-thread totals include prepared snapshots discarded before admission; lifetime of this scene owner.
        [[nodiscard]] Drawing::BalloonPublicationCopyTotals GetBalloonPublicationCopyTotals() const noexcept;

        [[nodiscard]] const std::shared_ptr<const PresentationGeneration>& GetGeneration() const noexcept;
    };
} // namespace OpenRCT2
