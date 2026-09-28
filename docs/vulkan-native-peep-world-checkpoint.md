# Native terrain and peep world pass

Implementation checkpoint in progress. The combined source batch compiles in build85. All 61 focused publication, asset-lifetime and foundation tests pass, including seven native publication tests. Device/ordering tests for this bounded peep pass remain unexecuted. Ordinary rendering now uses the separately measured [GPU terrain-only experiment](vulkan-gpu-only-terrain-experiment.md), without peeps. The last complete regression lane is build80/run62; installed checkpoint52 is unchanged.

## Replacement

The native publication profile owns independent graphical field groups and an immutable animation catalog. It does not copy Peep/Guest/Staff objects, manufacture compatibility balloons, build CPU entity spatial buckets, or allocate legacy lookup directories. A recycled legacy snapshot releases that storage. Owner mutation methods feed the existing coalesced worklist; publication acknowledges it only after successful preparation.

The shared Vulkan executor keeps slot-indexed lifecycle, motion, appearance and animation arrays in device memory. One batched transfer contains only changed groups. Shader passes scatter updates, build deterministic tile bins and generate peep body/accessory components in the same terrain traversal and parent ordering. The full visible scene is drawn every frame. Camera changes reuse instance state; there is no cached raster or lazy repainting.

Artwork uses shared, immutable asset generations. Each generation owns atlas leases and original-image/zoom metadata. Frame packets retain those leases until completion. Invalidation follows the actual image dependencies, including linked zoom parents, instead of invalidating every catalog for an unrelated UI image. Failed first submissions retain pending uploads for retry. Accepted submission advances the field cursor; abandoned recording does not.

Native motion capture reads authoritative endpoints through the tweener's identity-checked index, even when legacy rendering has temporarily moved the live object. Alpha and its current simulation tick are global camera inputs. Old snapshots cannot replay their history with a newer tick's alpha. Measured positive tick spans below half the uint32 range support skipped/network ticks and wrap. Zero or ambiguous spans publish the authoritative post position without history. The legacy visible-population scan and temporary coordinate writes still need removal.

## Admission and limits

This is an explicit diagnostic profile, default off. Its initial world scope is 32×32 terrain plus guests/staff, with zooms 0–1, no unsupported world families, view modes or LightFX. The target buffer bounds now accommodate 3840×2160. Unsupported native-only submissions fail before CPU world painting; they never silently omit entities or repeatedly switch publication profiles. Rejection can prepare asset-cache uploads, but records no partial world categories and cancels the frame.

The common ordering shader preserves the legacy linked arrangement. It still has quadratic worst cases, so moving that algorithm to the GPU does not itself establish scalable performance. Per-column parent/command capacity, the finite catalog/atlas bounds, and memory use must be measured under dense load. Ordinary paths, scenery, tracks/supports, vehicles, effects, larger maps and all viewport modes remain required before runtime admission can become universal.

## Checklist

- [x] Replace native publication's concrete entity copies and CPU spatial reconstruction with owned field snapshots and family capability counts.
- [x] Preserve authoritative motion endpoints and current-tick interpolation provenance.
- [x] Implement shared asset leases, dependency-scoped invalidation and failed-upload replay.
- [x] Integrate and compile the persistent field pipeline and common terrain/peep world pass (build85).
- [x] Pass the seven native publication regressions and five new asset lifecycle cases in the 61-test focused lane. Device transaction/retry behavior remains in the next item.
- [ ] Execute GPU field updates, cancellation/retry, ordering, camera-only reuse, empty reset and overflow tests with clean validation.
- [ ] Compare original-art scene images with the external reference and complete agent visual inspection.
- [ ] Demonstrate zero CPU viewport generation/arrangement/world sprite recording for admitted UI frames.
- [ ] Measure actual 4K dense moving-scene load for at least 3,000 ticks, distinguishing controlled fixture scaling from normal-park TPS.
- [ ] Complete remaining world families, remove the replaced CPU code and qualify ordinary parks plus displayed VSync pacing.

The earlier capture microbenchmark is documented in [the producer checkpoint](vulkan-peep-field-producer-checkpoint.md). It reports both improvements and regressions and does not measure this world pass.
