# Save-loading frame ownership regression — 2026-09-25

The deployed193 build could fail while loading a save from the title scene. The owner's dialog said `GPU texture cache frame is already active`. The preserved profile log contained `2026-09-25 13:25:23 UTC Vulkan render worker stopped: GPU atlas retirements require a frame boundary` (`obj/vulkan-parity/save-load-crash193-evidence`). This second message comes from shutdown retirement and is not sufficient to identify the first failure.

## Cause and correction

The warm-title save probe captured the earlier exception: `Retained peep loaded catalog slot is absent`. Loading replaces the object slots before importing the new park. Progress drawing was correctly inhibiting viewport rasterization, but `ViewportBeginPresentationFrame` still captured the old park's entities against the replacement catalog. The object-mutation guard covers the object-install operation, not the progress draw immediately after it returns.

Publication now honors viewport inhibition before accessing entity/catalog state or consuming dirty input. The separate LightFx capture also honors inhibition. Ordinary widgets continue rendering throughout loading. The first eligible post-load frame publishes the coherent new park through the existing epoch/catalog handling.

The original paint exception also escaped `Context::Draw` without ending its cache recording. A later draw then reported “frame is already active”; shutdown attempted retirement inside that abandoned frame. `AbortDraw` now discards an unsubmitted failed paint, and Context preserves and appends the original error to `render-error.log` before rethrowing. EndDraw retains its existing seal/retirement error handling. No error guard was disabled and no partially painted frame is submitted as recovery.

The probe additionally found slow immediate shutdown after Everything Park's first cold frame: the image cache was still alive when objects were unloaded. Each image invalidation could scan the large pending-upload list before the final completed frame's retirement was drained. Build195 rendered correctly but did not exit within180 seconds. Build196 destroys/drains the presentation engine after windows and auxiliary rendering have stopped, before object unloading. The same Everything Park probe now exits successfully. The quadratic-work explanation is supported by the invalidation/retirement code and the before/after behavior; no CPU stack capture was taken.

## Regression procedure and evidence

`scripts/rendering/run-title-loading.py --load-save-at-end PATH` copies and hashes a save into an isolated profile, runs two real title parks twice, then requests a saved-game load at the outer frame boundary. It checks load success, an accepted post-load presentation, screenshot creation, clean exit and absence of renderer-error logs. The diagnostic uses a hidden window and dummy audio, including suppression of fatal modal dialogs. It does not open or change the owner's profile or saves.

The initial194 probe called the loader inside title playback and ended with an access violation after recording the absent-catalog exception. That probe was corrected to hand the request to the outer frame boundary before qualifying the fix; its exit status is not represented as an exact reproduction of the owner's fatal-dialog sequence.

- [x] Preserve the original log and establish the first catalog mismatch with a silent probe.
- [x] Keep world publication and LightFx capture out of inhibited loading viewports.
- [x] Abort failed paint recording and preserve its original error.
- [x] Order presentation shutdown before object-image invalidation.
- [x] Build195:65 focused cache, publication and viewport-generation tests pass.
- [x] Build195: Corkscrew Park and Trinity Islands load, present and exit successfully at3840x2160; their screenshots were manually inspected.
- [x] Build195: Everything Park post-load screenshot manually inspected; shutdown timeout retained as failed evidence.
- [x] Build196: Everything Park warm-title load, post-load presentation and immediate shutdown pass at3840x2160 (`save-load196-everything-01`).
- [x] Final196 Corkscrew replay (`save-load196-corkscrew-01`) and65 focused tests (`native196-loading-lifecycle-01`) pass; final196 Everything and Corkscrew screenshots were manually inspected by an agent.
- [x] Final196 performance check:3840x2160,100 warmup ticks,12000 measured ticks, VSync enabled (`performance-entities196-12000-01`).315.279TPS,137.803 accepted presents/sec,0.720891ms mean drawing CPU,5.4679ms mean GPU, accepted-present p99<=14.3ms/max16.0817ms.
- [x] Rerun the archived193 executable under the same current configuration (`performance-entities193-control196-12000-01`):301.351TPS,124.081 accepted presents/sec,8.000865ms mean GPU. Both runs finish with checksum `07d58eaefde6aa6d000000000000000000000000`. These runs do not establish a performance improvement or meet the360TPS/144FPS goal; the old build also runs more slowly than its earlier recorded351TPS result. Host/driver variability remains unisolated. No renderer shader or simulation algorithm changed here.
- [x] Deploy qualified196 to `D:\Games\Independent\OpenRCT2Mod`:29 verified files,2 changed, backups retained; no automatic launch. Receipt: `obj/vulkan-parity/deploy-checkpoint196-01/receipt.json`. Source and this remaining-regression record are included in the fix commit.

This fixes loading/frame ownership. It does not claim to fix the still-open path-railing, helix/booth and large-half-loop depth contacts, nor establish full upstream pixel parity. Existing intended skirt differences remain accepted.
