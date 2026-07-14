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

    /**
     * Sole owner of main-window presentation publication.
     *
     * A caller begins at most one immutable generation per draw, may schedule preparation of its replacement after visible
     * paint work, and resets the whole scene at lifecycle boundaries. Map and entity snapshots never advance independently
     * outside this owner.
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
        bool BeginFrame(JobPool& jobs, EntityRegistry& entities, uint32_t drawCount, bool synchronousMapPublication);
        void ScheduleNext(JobPool& jobs, EntityRegistry& entities);
        void Reset(JobPool& jobs);

        [[nodiscard]] const std::shared_ptr<const PresentationGeneration>& GetGeneration() const noexcept;
    };
} // namespace OpenRCT2
