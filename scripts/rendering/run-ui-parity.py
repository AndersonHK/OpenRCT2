"""Run an isolated actual-main-UI capture and compare exact physical output.

The build receipt pins the instrumented executable. Software uses the preserved
reference assets; Vulkan overlays only receipt-verified compiled shader files.
Every divergent image still requires an agent's recorded manual inspection.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from PIL import Image, ImageChops

FIXTURE_STEPS = {
    "baseline": ("baseline",),
    "options-display": ("empty-ui", "options-display", "restored"),
    "world-dirty": ("dirty-baseline", "dirty-upper-canopy", "dirty-interior", "dirty-diagonal",
                    "dirty-lower-band", "dirty-full-restored"),
    "transparent-history": ("history-post-load", "history-opaque-first", "history-opaque-settled", "history-transparent-first",
                            "history-transparent-second", "history-transparent-settled", "history-opaque-restored"),
    "overlap": ("empty-ui", "research-front", "finances-front", "partially-clipped", "research-only", "restored"),
    "scroll": ("empty-ui", "scroll-top", "scroll-partial-row", "scroll-screen-clip", "scroll-resized", "restored"),
    "text": ("empty-ui", "text-empty", "wrapped-caret-start", "wrapped-caret-end", "submitted", "restored"),
    "light-night": ("light-paint-3", "light-paint-4", "light-paint-5", "light-paint-6"),
    "weather-precipitation": ("weather-clear", "weather-rain-light", "weather-rain-heavy", "weather-rain-occluded",
                              "weather-rain-restored", "weather-snow-light", "weather-snow-heavy", "weather-cleared"),
    "weather-palette": ("palette-clear", "palette-gloom-1", "palette-gloom-2", "palette-lightning",
                        "palette-lightning-recovered", "palette-restored"),
}
FONT_FAMILIES = ("font-ttf-arial-hinted", "font-sprite-fr", "font-ttf-arial-unhinted",
                 "font-sprite-ru", "font-ttf-ja", "font-family-fallback-vi")
FONT_STEPS = ("font-empty-ui", "font-localized-window", "font-caret-start", "font-caret-mid",
              "font-caret-end", "font-screen-clip", "font-restored")
for _family in FONT_FAMILIES:
    FIXTURE_STEPS[_family] = FONT_STEPS
for _ticks in (16, 32):
    FIXTURE_STEPS["world-motion" + str(_ticks)] = tuple(
        "motion-" + str(tick) + "-" + phase for tick in range(_ticks + 1) for phase in ("damage", "full"))
MOTION_FAMILIES = ("world-motion16", "world-motion32")
for _family in ("overlap", "scroll", "text", "world-dirty"):
    FIXTURE_STEPS[_family + "-incremental"] = FIXTURE_STEPS[_family]


LIFECYCLE_PHASES = ("lifecycle-software-initial", "lifecycle-vulkan-first", "lifecycle-vulkan-after-aux",
                    "lifecycle-vulkan-recreated", "lifecycle-software-return", "lifecycle-vulkan-return")
VULKAN_ONLY_LIFECYCLE_PHASES = ("lifecycle-vulkan-initial", "lifecycle-vulkan-first", "lifecycle-vulkan-after-aux",
                              "lifecycle-vulkan-recreated", "lifecycle-vulkan-recreated-again", "lifecycle-vulkan-return")


def validate_world_motion_samples(samples, ticks):
    """Capture every authoritative tick twice; damage/full differences are failures."""
    failures = []
    baseline = samples.get("motion-0-damage", {}).get("metadata", {})
    initial = baseline.get("simulationTicks")
    moved = False
    initial_records = baseline.get("worldMotion", {}).get("vehicles", {}).get("records", [])
    for tick in range(ticks + 1):
        pair = []
        for pass_index, phase in enumerate(("damage", "full")):
            name = "motion-" + str(tick) + "-" + phase
            sample = samples.get(name, {})
            meta = sample.get("metadata", {})
            motion = meta.get("worldMotion", {})
            count = motion.get("drawCount", {})
            census = motion.get("vehicles", {})
            records = census.get("records", [])
            consumed = motion.get("consumedVehicles", {})
            consumed_tick = motion.get("consumedSourceTick")
            if (type(consumed_tick) is not int or not 0 <= consumed_tick <= 0xffffffff
                    or consumed.get("schema") != 1 or consumed.get("columns") != census.get("columns")
                    or consumed.get("count", 0) <= 0
                    or consumed.get("count") != len(consumed.get("records", []))
                    or any(len(record) != 25 for record in consumed.get("records", []))):
                failures.append("Missing or malformed consumed publication " + name)
            ordinal = tick * 2 + pass_index + 3
            if (motion.get("schema") != 1 or type(initial) is not int or motion.get("initialTick") != initial
                    or meta.get("simulationTicks") != initial + tick or motion.get("tickOffset") != tick
                    or motion.get("totalTicks") != ticks or motion.get("pass") != phase
                    or motion.get("positions") != "authoritative; no tween"
                    or meta.get("cameraContract", {}).get("paintOrdinal") != ordinal
                    or meta.get("forcedFullInvalidation") is not (tick == 0 or phase == "full")
                    or type(count.get("before")) is not int or count.get("after") != count.get("before", -2) + 1
                    or census.get("schema") != 1 or census.get("count", 0) <= 0 or census.get("count") != len(records)
                    or len(census.get("columns", [])) != 25 or any(len(record) != 25 for record in records)
                    or len(census.get("scenarioRng", [])) != 2):
                failures.append("Malformed world motion contract " + name)
            for key in ("viewPosition", "rotation", "zoom", "cameraMode", "mainViewportBounds", "viewportFlags",
                        "paintStableSort", "landscapeSmoothing"):
                if meta.get(key) != baseline.get(key):
                    failures.append("World motion camera/policy changed " + name + ": " + key)
            if tick and records and initial_records:
                old = {record[0]:record[2:8] for record in initial_records}
                moved |= any(record[0] in old and record[2:8] != old[record[0]] for record in records)
            pair.append(sample)
        first, second = pair
        for key in ("simulationTicks", "paletteEffectFrame", "inputState"):
            if first.get("metadata", {}).get(key) != second.get("metadata", {}).get(key):
                failures.append("Motion pair input changed at tick " + str(tick) + ": " + key)
        if (first.get("metadata", {}).get("worldMotion", {}).get("vehicles")
                != second.get("metadata", {}).get("worldMotion", {}).get("vehicles")):
            failures.append("Motion pair vehicle/RNG state changed at tick " + str(tick))
        for key in ("consumedVehicles", "consumedSourceTick"):
            if (first.get("metadata", {}).get("worldMotion", {}).get(key)
                    != second.get("metadata", {}).get("worldMotion", {}).get(key)):
                failures.append("Motion pair consumed publication changed at tick " + str(tick) + ": " + key)
        for layer in ("indexedSha256", "rgbaSha256"):
            if not first.get(layer) or first.get(layer) != second.get(layer):
                failures.append("Motion damage/full repaint differs at tick " + str(tick) + ": " + layer)
    if not moved:
        failures.append("World motion fixture did not move or rotate any existing vehicle")
    return failures


def validate_tree_track_census(census, samples, motion):
    """Bind a read-only loaded census to the paused tick before the first paint."""
    failures = []
    integer = lambda value: type(value) is int and value >= 0
    if (census.get("schemaVersion") != 1 or census.get("kind") != "loaded-tree-wooden-track-scene-locator"
            or census.get("mutatedWorld") is not False or census.get("paused") is not True
            or not integer(census.get("simulationTicks"))
            or census.get("simulationTicksAfter") != census.get("simulationTicks")
            or census.get("unresolvedScenery") != 0 or census.get("searchRadiusTiles") != 8
            or census.get("candidateLimit") != 64):
        failures.append("Scene locator identity/paused-state contract differs")
    extent, map_size = census.get("viewportExtent", []), census.get("mapSize", [])
    if (len(extent) != 2 or any(not integer(v) or not 0 < v <= 16384 for v in extent)
            or len(map_size) != 2 or any(not integer(v) or not 0 < v <= 1024 for v in map_size)):
        failures.append("Scene locator map/viewport extent is invalid")
    counts, styles = census.get("treeCounts", {}), census.get("woodenStyleCounts", [])
    matched, wooden = census.get("matchedTrees"), census.get("woodenTrackElements")
    candidates = census.get("candidates", [])
    if (not isinstance(counts, dict) or any(not isinstance(k, str) or not k or not integer(v) or v == 0 for k,v in counts.items())
            or not isinstance(styles, list) or any(not isinstance(p, list) or len(p) != 2
                or not integer(p[0]) or not integer(p[1]) or p[1] == 0 for p in styles)
            or not integer(matched) or not integer(wooden) or not isinstance(candidates, list)):
        failures.append("Scene locator census count types are invalid")
    elif (sum(p[1] for p in styles) != wooden or matched > sum(counts.values())
            or len(candidates) != min(matched, 64) or (wooden == 0 and matched != 0)):
        failures.append("Scene locator counts disagree")
    for candidate in candidates:
        tree, track = candidate.get("tree", {}), candidate.get("track", {})
        if (tree.get("id") not in counts
                or tree.get("exactCherryObject") is not (tree.get("id") in (
                    "rct2ww.scenery_small.jachtree", "rct2ww.scenery_small.japchblo"))
                or not integer(candidate.get("tileDistanceSquared")) or candidate["tileDistanceSquared"] > 128):
            failures.append("Scene locator candidate does not belong to its census")
        for element in (tree, track):
            tile = element.get("tile", [])
            if (len(tile) != 2 or len(map_size) != 2
                    or any(not integer(value) or value >= map_size[axis] for axis,value in enumerate(tile))):
                failures.append("Scene locator candidate tile is outside the map")
        cameras = candidate.get("cameras", [])
        if ([(camera.get("rotation"), camera.get("zoom")) for camera in cameras]
                != [(rotation,zoom) for rotation in range(4) for zoom in range(2)]
                or any(len(camera.get("viewPosition", [])) != 2
                       or any(type(v) is not int for v in camera["viewPosition"]) for camera in cameras)):
            failures.append("Scene locator camera proposal inventory differs")
    if not samples:
        failures.append("Scene locator lacks a captured frame binding")
    for sample in samples.values():
        metadata = sample.get("metadata", {})
        tick = metadata.get("worldMotion", {}).get("initialTick") if motion else metadata.get("simulationTicks")
        binding = metadata.get("sceneLocator", {})
        if (tick != census.get("simulationTicks") or binding.get("simulationTicks") != tick
                or binding.get("viewportExtent") != extent):
            failures.append("Scene locator paused tick/viewport differs from captured frames")
            break
    return failures


def validate_world_dirty_samples(samples, incremental):
    """Static scene repainting must preserve pixels despite different invalidation domains."""
    failures = []
    phases = FIXTURE_STEPS["world-dirty"]
    for repetition in range(2):
        baseline = samples.get(phases[0] + "-" + str(repetition), {})
        baseline_meta = baseline.get("metadata", {})
        for index, phase in enumerate(phases):
            name = phase + "-" + str(repetition)
            sample = samples.get(name, {})
            metadata = sample.get("metadata", {})
            fact = metadata.get("inputState", {}).get("detail", {}).get("worldDirty", {})
            viewport = fact.get("viewport", [])
            if len(viewport) != 4 or any(type(v) is not int for v in viewport):
                failures.append("Missing dirty-world viewport evidence " + name)
                continue
            left, top, width, height = viewport
            expected = {
                "dirty-baseline": [], "dirty-full-restored": [],
                "dirty-upper-canopy": [[left+width//4, top+height//4, left+3*width//4, top+height//4+32]],
                "dirty-interior": [[left+width//2-16, top+height//2-16, left+width//2+16, top+height//2+16]],
                "dirty-diagonal": [[left+width//2+i*32, top+height//2+i*24,
                                    left+width//2+i*32+24, top+height//2+i*24+24] for i in range(-2,3)],
                "dirty-lower-band": [[left+width//4, top+3*height//4, left+3*width//4, top+3*height//4+32]],
            }[phase]
            full = index in (0, len(phases)-1)
            if (width < 384 or height < 320 or fact.get("schema") != 1 or fact.get("phase") != phase
                    or fact.get("mutation") != "none" or fact.get("requestedFullInvalidation") is not full
                    or fact.get("requestedRectangles") != expected
                    or fact.get("api") != ("GfxInvalidateScreen" if full else "IDrawingEngine::Invalidate")
                    or viewport != metadata.get("mainViewportBounds")
                    or metadata.get("forcedFullInvalidation") is not (not incremental or (repetition == 0 and index == 0))):
                failures.append("Malformed dirty-world invalidation contract " + name)
            for key in ("simulationTicks", "paletteEffectFrame", "viewPosition", "rotation", "zoom", "viewportFlags",
                        "paintStableSort", "landscapeSmoothing", "mainViewportBounds", "cameraMode"):
                if metadata.get(key) != baseline_meta.get(key):
                    failures.append("Static dirty-world input changed " + name + ": " + key)
            if metadata.get("cameraContract", {}).get("paintOrdinal") != 3 + repetition*len(phases) + index:
                failures.append("Dirty-world capture skipped or added a paint " + name)
            count = metadata.get("worldDirtyDrawCount", {})
            if (type(count.get("before")) is not int or type(count.get("after")) is not int
                    or count["after"] != count["before"] + 1):
                failures.append("Dirty-world did not retain its ordinary paint-counter transition " + name)
            for layer in ("rgbaSha256", "indexedSha256"):
                if not sample.get(layer) or sample[layer] != baseline.get(layer):
                    failures.append("Static dirty-world repaint changed " + name + ": " + layer)
    return failures


def validate_terrain_preparation_sequence(metadata_sequence):
    """Owner-recorded counters must belong to each named, unchanged settled packet."""
    if len(metadata_sequence) < 2:
        return ["Terrain preparation requires at least two settled captures"]
    failures = []
    previous = None
    for metadata in metadata_sequence:
        native = metadata.get("nativeTerrainFixture", {})
        source = native.get("source", {})
        value = native.get("preparation", {})
        keys = ("frameNumber", "epoch", "materialMapCopies", "spriteCatalogBuilds", "residencyRebinds")
        if (type(value.get("schema")) is not int or value.get("schema") != 1 or any(type(value.get(k)) is not int or value[k] < 0 for k in keys)
                or value.get("epoch", 0) <= 0 or value.get("materialMapCopies", 0) <= 0
                or value.get("spriteCatalogBuilds", 0) <= 0):
            failures.append("Terrain preparation counters are missing, malformed or uninitialized")
            continue
        if value["frameNumber"] != metadata.get("frameNumber") or value["epoch"] != source.get("epoch"):
            failures.append("Terrain preparation counters do not identify the named packet publication")
        if previous is not None:
            old, old_source = previous
            if (value["epoch"] != old["epoch"] or source.get("chunkRevisions") != old_source.get("chunkRevisions")
                    or source.get("materials") != old_source.get("materials")
                    or source.get("materialRevision") != old_source.get("materialRevision")):
                failures.append("Terrain preparation sequence changed its resident publication")
            if value["frameNumber"] <= old["frameNumber"]:
                failures.append("Terrain preparation sequence did not advance named frames")
            if any(value[k] != old[k] for k in ("materialMapCopies", "spriteCatalogBuilds")):
                failures.append("Settled terrain rebuilt its material map or sprite catalog")
            if value["residencyRebinds"] <= old["residencyRebinds"]:
                failures.append("Settled terrain did not retain and rebind its catalog")
        previous = value, source
    return failures

def validate_native_terrain_sample(metadata, expected_tiles, fallback):
    failures = []
    native = metadata.get("nativeTerrainFixture", {})
    cpu = metadata.get("cpuViewportPaint", {})
    source = native.get("source", {})
    scenes = native.get("scenes", [])
    uploads = native.get("uploads", {})
    if source.get("tiles") != expected_tiles or source.get("bounded") is not True or source.get("epoch", 0) <= 0:
        failures.append("Named terrain publication differs from accepted raw input02")
    if metadata.get("balloonPublication", {}).get("entityCount") != 0:
        failures.append("Terrain publication unexpectedly contains entities")
    if native.get("schema") != 1 or native.get("viewports") != len(scenes):
        failures.append("Missing/malformed terrain diagnostic contract")
    if any(type(cpu.get(k)) is not int for k in ("generate", "arrange", "draw")):
        failures.append("Actual CPU viewport counters are missing")
    elif fallback:
        if not all(cpu[k] > 0 for k in ("generate", "arrange", "draw")) or scenes:
            failures.append("Terrain decline did not perform positive CPU world paint")
    elif any(cpu[k] != 0 for k in ("generate", "arrange", "draw")) or not scenes:
        failures.append("Native terrain did not bypass all CPU world column work")
    if uploads.get("viewports") != len(scenes) or any(uploads.get(k) != 0 for k in ("tiles", "materials", "sprites")):
        failures.append("Settled terrain frame uploaded state or viewport accounting differs")
    if scenes and (native.get("atlasLease", 0) <= 0 or uploads.get("statusReadback", 0) <= 0):
        failures.append("Native terrain lacks real shared-atlas lease or compact status copy")
    catalog = {(m["kind"],m["slot"]):m for m in source.get("materials", [])}
    for scene in scenes:
        camera = scene.get("camera", [])
        bounds, pose = metadata.get("mainViewportBounds", []), metadata.get("viewPosition", [])
        if (len(camera) != 11 or len(bounds) != 4 or len(pose) != 2
                or not all(type(v) is int for v in camera+bounds+pose)
                or camera[6:10] != [metadata.get("rotation"),metadata.get("zoom"),0,0]):
            failures.append("Terrain command camera contract is absent or unsupported")
        else:
            x,y,w,h,cx,cy,rotation,zoom,transparent,stable,depth = camera
            bx,by,bw,bh = bounds
            if (w <= 0 or h <= 0 or cx < bx or cy < by or cx+w > bx+bw or cy+h > by+bh
                    or x != (pose[0] >> zoom)+cx-bx or y != (pose[1] >> zoom)+cy-by or depth <= 0):
                failures.append("Terrain GPU clip/camera differs from the actual main viewport")
        if (scene.get("epoch") != source.get("epoch") or scene.get("chunkRevisions") != source.get("chunkRevisions")
                or scene.get("materialRevision", 0) <= 0 or scene.get("spriteRevision", 0) <= 0):
            failures.append("GPU terrain command has stale publication identity")
        tiles, materials = scene.get("tiles", []), scene.get("materials", [])
        if len(tiles) != 1024:
            failures.append("GPU terrain command has wrong raw tile count")
            continue
        for tile, original in zip(tiles, expected_tiles):
            if tile[:3] != original[:3] or tile[5] != original[5]:
                failures.append("GPU terrain raw facts differ from source")
                break
            if tile[5] == 1:
                for kind, index in ((1,3),(2,4)):
                    if type(tile[index]) is not int or not 0 <= tile[index] < len(materials):
                        failures.append("GPU terrain material index outside owned table")
                        continue
                    a,b = materials[tile[index]],catalog.get((kind,original[index]),{})
                    if any(a.get(k) != b.get(k) for k in ("kind","base","count","selectors")) or a.get("flags") != 0:
                        failures.append("GPU terrain material mapping differs from published object catalog")
                        break
        if not scene.get("sprites"):
            failures.append("GPU terrain has no owned shared-atlas sprite metadata")
    return list(dict.fromkeys(failures))


def validate_shared_lifecycle(report, samples, indices, rgba, vulkan_only=False):
    """Independent lifecycle topology and byte-pattern checks; no device references."""
    failures = []
    if not isinstance(report, dict):
        return ["Shared lifecycle report must be an object"]
    phase_names = VULKAN_ONLY_LIFECYCLE_PHASES if vulkan_only else LIFECYCLE_PHASES
    if vulkan_only:
        if (report.get("version") != 2 or report.get("status") != "pass"
                or report.get("rendererContract") != "vulkan-only-v1"
                or report.get("ownerCreatedAfterVulkanStartup") is not True):
            failures.append("Shared lifecycle lacks the pinned Vulkan-only startup proof")
    elif (report.get("version") != 1 or report.get("status") != "pass"
            or report.get("ownerUncreatedAfterSoftwareStartup") is not True):
        failures.append("Shared lifecycle lacks a passing deferred-owner proof")
    phases = report.get("phases", [])
    if not isinstance(phases, list) or [p.get("name") for p in phases if isinstance(p, dict)] != list(phase_names):
        return failures + ["Shared lifecycle phase sequence is missing or malformed"]
    device_phases = phases if vulkan_only else phases[1:]
    first = device_phases[0]
    for key in ("contextIdentity", "deviceIdentity"):
        if type(first.get(key)) is not int or first[key] <= 0 or any(p.get(key) != first[key] for p in device_phases):
            failures.append("Shared device identity changed or is absent: " + key)
        if not vulkan_only and phases[0].get(key) is not None:
            failures.append("Initial software phase unexpectedly observed a device")
    ids = [p.get("windowId") for p in phases]
    if (any(type(value) is not int or value <= 0 for value in ids) or ids[2] != ids[1]
            or len({ids[i] for i in (0, 1, 3, 4, 5) if type(ids[i]) is int}) != 5):
        failures.append("Real window identities do not prove the required recreation topology")
    tick = phases[0].get("simulationTicks")
    for index, phase in enumerate(phases):
        name = phase_names[index]
        expected_renderer = "softwareWithHardwareDisplay" if not vulkan_only and index in (0, 4) else "vulkan"
        metadata = samples.get(name, {}).get("metadata", {})
        if (phase.get("renderer") != expected_renderer or metadata.get("renderer") != expected_renderer
                or type(tick) is not int or phase.get("simulationTicks") != tick or metadata.get("simulationTicks") != tick):
            failures.append("Lifecycle renderer or paused tick mismatch: " + name)
        for key in ("rgbaSha256", "indexedSha256"):
            if not samples.get(name, {}).get(key) or samples[name][key] != samples.get(phase_names[0], {}).get(key):
                failures.append("Lifecycle screen changed across transitions: " + name + ": " + key)
    auxiliary = report.get("auxiliary", {})
    if (auxiliary.get("exactPattern") is not True or auxiliary.get("indexedBytes") != 4096
            or auxiliary.get("rgbaBytes") != 16384 or any(type(auxiliary.get(key)) is not int or auxiliary[key] <= 0
                for key in ("submissionId", "targetId", "targetGeneration"))):
        failures.append("Shared auxiliary result identity or dimensions are invalid")
    expected = bytes(37 if 7 <= x <= 29 and 11 <= y <= 43 else 17 for y in range(64) for x in range(64))
    expected_rgba = bytes(channel for value in expected for channel in (value, value, value, 255))
    if indices != expected or rgba != expected_rgba:
        failures.append("Shared auxiliary buffers differ from the independent indexed/RGBA pattern")
    shutdown = report.get("shutdown", {})
    for key in ("ownerExpiredWithRecorderAlive", "deviceExpiredWithRecorderAlive", "sdlVideoStopped",
                "lateSubmitRejected", "strongDeviceObservationsReleased"):
        if shutdown.get(key) is not True:
            failures.append("Shared lifecycle shutdown failed: " + key)
    return failures


def validate_camera_sample(metadata, args):
    """Check requested camera admission independently of cross-renderer equality."""
    failures = []
    if metadata.get("cameraMode") != args.camera_mode:
        failures.append("Camera mode differs from the requested mode")
    contract = metadata.get("cameraContract", {})
    if contract.get("version") != 2 or contract.get("warmupPaints") != (0 if getattr(args,"fixture",None) == "transparent-history" else 2):
        return failures + ["Capture lacks the required camera contract and two verified warmup paints"]
    def valid_pose(pose):
        return (isinstance(pose, dict) and isinstance(pose.get("viewPosition"), list)
                and len(pose["viewPosition"]) == 2 and all(type(v) is int for v in pose["viewPosition"])
                and type(pose.get("rotation")) is int and 0 <= pose["rotation"] <= 3
                and type(pose.get("zoom")) is int)
    loaded, expected = contract.get("loadedPose"), contract.get("expectedPose")
    loaded_target = contract.get("loadedTargetPose")
    actual = {key: metadata.get(key) for key in ("viewPosition", "rotation", "zoom")}
    if not all(valid_pose(pose) for pose in (loaded, loaded_target, expected, actual)):
        return failures + ["Camera pose metadata is malformed"]
    if (actual != expected or contract.get("afterWarmupPose") != expected
            or contract.get("afterPaintPose") != expected
            or type(contract.get("paintOrdinal")) is not int or contract["paintOrdinal"] < (1 if getattr(args,"fixture",None) == "transparent-history" else 3)):
        failures.append("Actual camera drifted from its expected pose during warmup/capture")
    if args.camera_mode == "saved":
        if expected != loaded_target or any(loaded_target[key] != loaded[key] for key in ("rotation", "zoom")):
            failures.append("Saved camera differs from the authoritative loaded window target/rotation/zoom")
    else:
        if actual["rotation"] != args.rotation or actual["zoom"] != args.zoom:
            failures.append("Actual camera rotation/zoom differs from explicit request")
        requested_position = [args.view_x, args.view_y]
        for axis, requested in enumerate(requested_position):
            required = loaded_target["viewPosition"][axis] if requested is None else requested
            if actual["viewPosition"][axis] != required:
                failures.append("Actual camera " + ("x" if axis == 0 else "y") + " differs from explicit request/loaded target default")
    return failures


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_font_samples(family, renderer, samples):
    """Reject empty/incorrect font coverage even when both renderers agree."""
    failures = []
    ttf = family not in ("font-sprite-fr", "font-sprite-ru")
    japanese = family == "font-ttf-ja"
    fallback = family == "font-family-fallback-vi"
    hinted = family != "font-ttf-arial-unhinted"
    locale = "ja-JP" if japanese else "ru-RU" if family == "font-sprite-ru" else "vi-VN" if ttf else "fr-FR"
    threshold = (60 if japanese else 40) if ttf and hinted else 0
    filename = "msgothic.ttc" if japanese else "arial.ttf"
    font_name = "MS Gothic" if japanese else "Arial"
    font_sha = ("4bde3e6392b96910fb59094c6c1a4dbfae18fee78d0bf13dc30616837c4f95db" if japanese else
                "c9b76220a5be42ead4733611e417cd65c5fd8aeaa33eb56576ac378a37d130a1")
    for ordinal, name in enumerate(FONT_STEPS, 3):
        if name not in samples:
            failures.append("Missing required font capture " + name)
            continue
        metadata = samples[name]["metadata"]
        fonts = metadata.get("fonts", {})
        if fonts.get("trueTypeEnabled") is not ttf or fonts.get("locale") != locale:
            failures.append("Font language/mode mismatch " + name)
        if fonts.get("paintOrdinal") != ordinal:
            failures.append("Font paint ordinal mismatch " + name)
        text = fonts.get("text", "")
        boundaries = [0]
        for character in text:
            boundaries.append(boundaries[-1] + len(character.encode("utf-8")))
        if not text or fonts.get("codepointBoundaries") != boundaries or fonts.get("textByteLength") != boundaries[-1]:
            failures.append("Invalid UTF-8 font input/boundaries " + name)
        if not fonts.get("languageFiles") or not fonts.get("cmap") or fonts.get("wrappedLineBreaks", 0) <= 0:
            failures.append("Missing loaded language/glyph/wrapping evidence " + name)
        for glyph in fonts.get("cmap", []):
            if not glyph.get("fontProvidesGlyph") and not glyph.get("intentionalSpriteSymbol"):
                failures.append("Unsupported fixture codepoint " + name)
        styles = fonts.get("styles", [])
        if ttf:
            if len(styles) != 3 or {style.get("style") for style in styles} != {0, 1, 2} or any(
                    style.get("filename") != filename or style.get("fontName") != font_name
                    or style.get("sha256") != font_sha
                    or style.get("pointSize") != (10 if fallback and style.get("style") == 2 else 12)
                    or style.get("lineHeight") != ((9 if style.get("style") == 2 else 12) if fallback else 14)
                    or style.get("offset") != [0, -1] or style.get("hintingThreshold") != (60 if japanese else 40)
                    or style.get("actualHinting") != int(hinted) or style.get("faceIndex") != 0 for style in styles):
                failures.append("Pinned font profile missing or changed " + name)
        elif styles:
            failures.append("Sprite-only fixture unexpectedly has TrueType styles " + name)
        if family not in ("font-ttf-arial-hinted", "font-sprite-fr"):
            requested = fonts.get("requestedProfile", {})
            expected_filename = "OpenRCT2-parity-intentionally-missing.ttf" if fallback else filename if ttf else ""
            expected_name = "OpenRCT2 Parity Intentionally Missing" if fallback else font_name if ttf else ""
            if (requested.get("filename") != expected_filename or requested.get("fontName") != expected_name
                    or requested.get("enableHinting") is not hinted
                    or requested.get("effectiveMediumThreshold") != threshold
                    or requested.get("fallbackExpected") is not fallback):
                failures.append("Requested font profile differs " + name)
            absent = fonts.get("requiredAbsentFonts", [])
            if fallback:
                if ({item.get("filename") for item in absent} !=
                        {"OpenRCT2-parity-intentionally-missing.ttf", "arialuni.ttf"}
                        or any(item.get("exists") is not False or not item.get("resolvedPath") for item in absent)):
                    failures.append("Required font-family fallback absence proof missing " + name)
            elif absent:
                failures.append("Unexpected missing-font prerequisites " + name)
            if japanese and (fonts.get("pinnedCollectionFace", {}).get("index") != 0
                             or fonts.get("pinnedCollectionFace", {}).get("family") != "MS Gothic"):
                failures.append("Pinned Japanese TTC face proof missing " + name)
        input_text = metadata.get("inputState", {}).get("fontTextInput", {})
        caret = input_text.get("caret")
        if caret not in boundaries:
            failures.append("Caret is not a UTF-8 codepoint boundary " + name)
        expected_caret = (boundaries[-1] if name in ("font-caret-end", "font-screen-clip") else
                          len(("\u89b3\u89a7\u8eca" if japanese else "AVATAR Caf\u00e9" if ttf else "Caf\u00e9").encode("utf-8")) if name == "font-caret-mid" else 0)
        if caret != expected_caret:
            failures.append("Caret differs from prescribed font step " + name)
        if renderer == "vulkan":
            commands = metadata.get("ttfCommands", {})
            has_ttf = commands.get("opaque", 0) + commands.get("transparent", 0) > 0
            if has_ttf != ttf:
                failures.append("Actual GPU TTF route differs from font fixture " + name)
            if ttf and name in ("font-caret-start", "font-caret-mid", "font-caret-end", "font-screen-clip"):
                bounds = input_text.get("textBounds")
                visible = bounds and any(len(command) == 5 and command[4] == threshold
                    and max(bounds[0], command[0]) < min(bounds[2], command[2])
                    and max(bounds[1], command[1]) < min(bounds[3], command[3])
                    for command in commands.get("clippedBoundsAndThreshold", []))
                if not visible:
                    failures.append("Actual dialog TTF command coverage absent " + name)
    for previous, current in zip(FONT_STEPS[:5], FONT_STEPS[1:6]):
        if previous in samples and current in samples:
            for layer in ("rgbaSha256", "indexedSha256"):
                if samples[previous][layer] == samples[current][layer]:
                    failures.append("Font UI transition absent " + previous + " / " + current + ": " + layer)
    if "font-empty-ui" in samples and "font-restored" in samples:
        for layer in ("rgbaSha256", "indexedSha256"):
            if samples["font-empty-ui"][layer] != samples["font-restored"][layer]:
                failures.append("Font restored baseline differs: " + layer)
    return failures


def validate_scaled_font_sample(metadata, reference_metadata, actual_rgba, actual_indices, reference_rgba, reference_indices,
                                scale, width, height):
    """Validate realized scaling and logical invariance; actual frozen display remains the pixel oracle."""
    failures = []
    expected_quality = 0 if scale == 2 else 2
    expected_scaling = {"fixtureVersion": 1, "requestedWindowScale": scale,
                        "requestedWindowExtent": [width, height], "requiredLogicalExtent": [960, 640],
                        "expectedQuality": expected_quality,
                        "qualityPolicy": "production UiContext: integer nearest; fractional smooth nearest with linear final stage"}
    if metadata.get("scaling") != expected_scaling or metadata.get("windowScale") != scale:
        failures.append("Requested/actual scaling metadata mismatch")
    if (metadata.get("logicalExtent") != [960, 640] or metadata.get("physicalExtent") != [width, height]
            or metadata.get("scaleQuality") != expected_quality):
        failures.append("Realized scaled canvas/filter/physical extent mismatch")
    if (reference_metadata.get("windowScale") != 1 or reference_metadata.get("logicalExtent") != [960, 640]
            or reference_metadata.get("physicalExtent") != [960, 640] or reference_metadata.get("scaling") is not None):
        failures.append("Unscaled reference does not represent the required scale-1 canvas")
    for key in ("fixture", "fixtureVersion", "compositionFamily", "viewPosition", "rotation", "zoom", "cameraMode",
                "assetState", "simulationTicks", "paletteEffectFrame", "landscapeSmoothing", "viewportFlags",
                "language", "themePreset", "inputState", "step", "repetition", "fonts"):
        if metadata.get(key) != reference_metadata.get(key):
            failures.append("Unscaled reference input mismatch: " + key)
    if actual_indices != reference_indices:
        failures.append("Scaled presentation changed the logical indexed canvas")
    evidence = {"windowScale": scale, "quality": expected_quality, "logicalBytesEqual": actual_indices == reference_indices}
    if len(reference_rgba) != 960 * 640 * 4 or len(actual_rgba) != width * height * 4:
        failures.append("Scaled/unscaled raw physical length mismatch")
        return failures, evidence
    source = Image.frombytes("RGBA", (960, 640), reference_rgba)
    nearest = source.resize((width, height), Image.Resampling.NEAREST)
    actual = Image.frombytes("RGBA", (width, height), actual_rgba)
    differing = sum(a != b for a, b in zip(actual.getdata(), nearest.getdata()))
    novel = set(actual.getdata()) - set(source.getdata())
    evidence.update({"nearestExpansionDifferingPixels": differing, "novelPhysicalColours": len(novel),
                     "nearestExpansionSha256": hashlib.sha256(nearest.tobytes()).hexdigest(),
                     "interpretation": "nearest expansion is a non-vacuity check, not the fractional pixel oracle"})
    if scale == 2 and differing:
        failures.append("Integer 2x presentation differs from exact nearest expansion")
    if scale != 2 and not differing:
        failures.append("Fractional presentation did not differ from nearest expansion")
    return failures, evidence


def validate_native_balloon_sample(metadata, expect_fallback):
    """Distinguish complete fallback controls from positive native coverage."""
    native = metadata.get("nativeBalloonFixture", {})
    publication = metadata.get("balloonPublication", {})
    if expect_fallback:
        if (publication.get("gpuAdmission") is not False
                or any(native.get(key) != 0 for key in (
                    "viewports", "submittedViewports", "sourceUploadBytes", "spriteUploadBytes"))):
            return ["Expected complete native balloon fallback"]
        # Hidden entities can correctly have zero CPU sprite calls. Exact independent pixels remain mandatory.
        return []
    if (publication.get("gpuAdmission") is not True
            or native.get("viewports", 0) <= 0 or native.get("cpuBalloonSpriteCalls") != 0
            or native.get("submittedViewports") != native.get("viewports")
            or native.get("uploadEpoch") != publication.get("epoch")
            or native.get("uploadSequence") != publication.get("sequence")):
        return ["Incomplete native balloon admission"]
    return []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--renderer", choices=("software", "vulkan"), default="software")
    parser.add_argument("--fixture", choices=tuple(FIXTURE_STEPS), default="baseline")
    parser.add_argument("--shared-service-lifecycle", action="store_true",
                        help="Six paused production-owner phases including WSI recreation, auxiliary rendering and shutdown")
    parser.add_argument("--shader-build-receipt", type=Path)
    parser.add_argument("--rct2-path", type=Path, required=True)
    parser.add_argument("--rct1-path", type=Path)
    parser.add_argument("--park", type=Path)
    parser.add_argument("--compare-run", type=Path)
    parser.add_argument("--compare-full-run", type=Path, help="Also require equality to the same family's full-invalidation captures")
    parser.add_argument("--fresh-repeat", action="store_true", help="Compare this fresh process with --compare-run from the identical renderer build")
    parser.add_argument("--validation", action="store_true")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=640)
    parser.add_argument("--window-scale", type=float, choices=(1, 1.25, 1.5, 2), default=1)
    parser.add_argument("--compare-unscaled-run", type=Path,
                        help="Required for scaled font fixtures: matching scale-1 family and logical canvas")
    parser.add_argument("--rotation", type=int, choices=range(4), default=0)
    parser.add_argument("--zoom", type=int, choices=range(-2, 4), default=0)
    parser.add_argument("--camera-mode", choices=("explicit", "saved"), default="explicit",
                        help="Saved preserves the park's camera rotation, zoom and position")
    parser.add_argument("--view-x", type=int)
    parser.add_argument("--view-y", type=int)
    parser.add_argument("--landscape-smoothing", choices=("true", "false"), default="true")
    parser.add_argument("--require-world-surfaces", choices=("true", "false", "any"), default="any")
    parser.add_argument("--history-clear-zero", action="store_true", help="Diagnostic transparent-history control only; clear index0 before each paint")
    parser.add_argument("--gpu-terrain", action="store_true", help="Closed input02 complete-terrain diagnostic admission")
    parser.add_argument("--terrain-input-receipt", type=Path)
    parser.add_argument("--expect-gpu-terrain-fallback", action="store_true")
    parser.add_argument("--require-cpu-world-paint", action="store_true", help="Positive counter control using current software")
    parser.add_argument("--gpu-balloons", action="store_true", help="Bounded diagnostic native balloon fixture; requires retained publication")
    parser.add_argument("--expect-gpu-balloon-fallback", action="store_true",
                        help="Require the attempted native balloon path to decline; exact reference comparison remains mandatory")
    parser.add_argument("--retained-balloons", action="store_true", help="Diagnostic Vulkan publication only; no GPU admission")
    parser.add_argument("--require-balloon-count", type=int, help="Require exact positive immutable balloon census in every lane")
    parser.add_argument("--balloon-input-receipt", type=Path, help="Accepted frozen-export manifest; required with positive balloon census")
    parser.add_argument("--paint-stable-sort", choices=("true", "false"), default="false")
    parser.add_argument("--cherry-fixture", choices=("rct2ww.scenery_small.japchblo", "rct2ww.scenery_small.jachtree"))
    parser.add_argument("--viewport-flags", type=lambda value: int(value, 0), default=0)
    args = parser.parse_args()
    if args.cherry_fixture and args.fixture not in ("world-dirty",) + MOTION_FAMILIES:
        raise SystemExit("Cherry substitution requires an explicit world ordering fixture")
    if args.fixture in ("world-dirty", "world-dirty-incremental") + MOTION_FAMILIES and (
            args.gpu_terrain or args.gpu_balloons or args.retained_balloons or args.shared_service_lifecycle
            or args.require_balloon_count is not None or args.viewport_flags or args.window_scale != 1
            or args.width < 384 or args.height < 320
            or (args.renderer == "vulkan" and (not args.validation or not args.compare_run or not args.shader_build_receipt))):
        raise SystemExit("Dirty-world requires an isolated ordinary pinned park/camera and exact validated Vulkan comparison")
    if args.history_clear_zero and args.fixture != "transparent-history":
        raise SystemExit("--history-clear-zero is restricted to transparent-history")
    if args.fixture == "transparent-history" and (
            args.gpu_terrain or args.gpu_balloons or args.retained_balloons or args.shared_service_lifecycle
            or args.require_balloon_count is not None or args.viewport_flags or args.landscape_smoothing != "false"
            or args.camera_mode != "explicit" or (args.view_x,args.view_y,args.rotation,args.zoom) != (-479,145,0,0)
            or (args.width,args.height,args.window_scale) != (960,640,1)
            or (args.renderer == "vulkan" and (not args.validation or not args.compare_run or not args.shader_build_receipt))):
        raise SystemExit("Transparent history requires ordinary isolated input02 r0z0 camera, flags0, smoothing off and exact validated Vulkan")
    if args.gpu_terrain and (args.renderer != "vulkan" or not args.terrain_input_receipt or not args.compare_run
            or not args.validation or not args.shader_build_receipt or args.gpu_balloons or args.retained_balloons
            or args.shared_service_lifecycle or args.camera_mode != "explicit" or args.view_x is None or args.view_y is None
            or args.zoom not in (0, 1) or args.window_scale != 1 or args.width > 1024 or args.height > 1024
            or args.fixture not in ("baseline", "overlap", "overlap-incremental")):
        raise SystemExit("Native terrain requires input02 receipt, explicit bounded camera, isolated validated Vulkan and exact reference")
    if args.gpu_terrain and not args.expect_gpu_terrain_fallback and (args.landscape_smoothing != "false" or args.viewport_flags or args.paint_stable_sort == "true"):
        raise SystemExit("Positive terrain admission requires smoothing off, ordinary viewport flags and unstable sort")
    if args.expect_gpu_terrain_fallback and not args.gpu_terrain:
        raise SystemExit("Terrain fallback requires --gpu-terrain")
    if args.require_cpu_world_paint and (args.renderer != "software" or not args.compare_run):
        raise SystemExit("CPU paint counter control requires current software with an exact reference")
    if args.shared_service_lifecycle and (args.renderer != "vulkan" or args.fixture != "baseline"
            or not args.compare_run or not args.validation or not args.shader_build_receipt or args.require_world_surfaces != "any"
            or args.gpu_balloons or args.retained_balloons or args.require_balloon_count is not None
            or (args.width, args.height, args.window_scale) != (960, 640, 1)):
        raise SystemExit("Shared lifecycle requires validated Vulkan baseline, exact reference, 960x640 scale1 and no native admission flags")
    if args.camera_mode == "saved" and (args.view_x is not None or args.view_y is not None):
        raise SystemExit("Saved camera cannot have explicit view coordinates")
    if args.window_scale != 1:
        if (args.fixture not in FONT_FAMILIES or not args.compare_unscaled_run
                or (args.width, args.height) != (int(960 * args.window_scale), int(640 * args.window_scale))):
            raise SystemExit("Scaled fonts require --compare-unscaled-run and physical dimensions960x640 times scale")
    elif args.compare_unscaled_run:
        raise SystemExit("--compare-unscaled-run requires non-unit --window-scale")
    if args.require_balloon_count is not None and args.require_balloon_count <= 0:
        raise SystemExit("--require-balloon-count must be positive")
    if args.expect_gpu_balloon_fallback and (not args.gpu_balloons or not args.compare_run):
        raise SystemExit("--expect-gpu-balloon-fallback requires --gpu-balloons and --compare-run")
    if args.gpu_balloons and not args.retained_balloons:
        raise SystemExit("--gpu-balloons requires --retained-balloons")
    if args.retained_balloons and (args.renderer != "vulkan" or not args.require_balloon_count):
        raise SystemExit("--retained-balloons requires Vulkan and a positive --require-balloon-count")
    if (args.require_balloon_count is not None) != (args.balloon_input_receipt is not None):
        raise SystemExit("Positive balloon count and --balloon-input-receipt must be supplied together")
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if output.exists() or root not in output.parents:
        raise SystemExit("Use a new output directory inside the workspace")
    receipt_path = args.build_receipt.resolve(strict=True)
    build = json.loads(receipt_path.read_text(encoding="utf-8"))
    if build["status"] != "pass":
        raise SystemExit("Capture build receipt did not pass")
    # Select the changed contract only from the exact header compiled into this build.
    # External frozen source retains its software/switching contract without the macro.
    renderer_header_name = "src/openrct2/drawing/IDrawingEngine.h"
    renderer_header = Path(build["sourceRoot"]) / renderer_header_name
    renderer_header_digest = build.get("sourceSha256", {}).get("source/" + renderer_header_name)
    if not renderer_header_digest or not renderer_header.is_file() or sha256(renderer_header) != renderer_header_digest:
        raise SystemExit("Renderer contract header does not match the compiled source manifest")
    vulkan_only = re.search(r"(?m)^\s*#\s*define\s+OPENRCT2_VULKAN_ONLY\s+1\s*$",
                            renderer_header.read_text(encoding="utf-8")) is not None
    renderer_contract = {"vulkanOnly": vulkan_only, "header": str(renderer_header),
                         "sha256": renderer_header_digest, "macro": "OPENRCT2_VULKAN_ONLY"}
    if vulkan_only and args.renderer != "vulkan":
        raise SystemExit("This receipt-pinned Vulkan-only build requires --renderer vulkan")
    build_root = receipt_path.parent
    for name, digest in build["artifactSha256"].items():
        path = (build_root / name).resolve(strict=True)
        if build_root not in path.parents or sha256(path) != digest:
            raise SystemExit("Capture binary/library changed: " + name)
    for name, digest in build.get("runtimeDllSha256", {}).items():
        if sha256(build_root / "bin" / Path(name).name) != digest:
            raise SystemExit("Capture runtime library changed: " + name)
    references = {}
    for reference_run in (args.compare_run, args.compare_full_run, args.compare_unscaled_run):
        if reference_run is None:
            continue
        reference_summary = json.loads((reference_run / "summary.json").read_text(encoding="utf-8"))
        if reference_summary["status"] != "pass":
            raise SystemExit("Reference capture run did not pass")
        references[reference_run] = reference_summary
    if args.shared_service_lifecycle:
        previous = references[args.compare_run]
        if args.fresh_repeat:
            if previous.get("sharedServiceLifecycle") is not True:
                raise SystemExit("Lifecycle fresh repeat needs a prior passing lifecycle run")
        elif (previous.get("renderer") != "software" or previous.get("fixture") != "baseline"
                or not previous.get("oracleSourceReceipt") or previous.get("sharedServiceLifecycle")
                or previous.get("requiredCaptures") != ["baseline-0", "baseline-1"]):
            raise SystemExit("Lifecycle first run requires a qualified frozen software baseline")
    if args.compare_full_run and not args.fixture.endswith("-incremental"):
        raise SystemExit("--compare-full-run applies only to an incremental fixture")
    if args.fresh_repeat:
        if not args.compare_run:
            raise SystemExit("--fresh-repeat requires --compare-run")
        previous = references[args.compare_run]
        if previous.get("historyClearZero", False) != args.history_clear_zero:
            raise SystemExit("Fresh repeat requires identical history clear policy")
        if previous.get("paintStableSort", False) != (args.paint_stable_sort == "true"):
            raise SystemExit("Fresh repeat requires identical paint stable-sort policy")
        if bool(previous.get("sharedServiceLifecycle")) != args.shared_service_lifecycle:
            raise SystemExit("Fresh repeat requires identical lifecycle selection")
        if previous.get("expectGpuBalloonFallback", False) != args.expect_gpu_balloon_fallback:
            raise SystemExit("Fresh repeat requires identical native balloon fallback expectation")
        if (previous.get("gpuTerrain", False) != args.gpu_terrain
                or previous.get("expectGpuTerrainFallback", False) != args.expect_gpu_terrain_fallback):
            raise SystemExit("Fresh repeat requires identical terrain admission expectation")
        if previous.get("gpuBalloons", False) != args.gpu_balloons:
            raise SystemExit("Fresh repeat requires identical native balloon admission")
        if previous.get("retainedBalloons", False) != args.retained_balloons:
            raise SystemExit("Fresh repeat requires identical publication profile")
        if (args.shared_service_lifecycle or args.gpu_terrain or args.fixture in ("transparent-history", "world-dirty", "world-dirty-incremental") + MOTION_FAMILIES) and ((previous.get("shaderBuildReceipt") or {}).get("sha256") != (sha256(args.shader_build_receipt) if args.shader_build_receipt else None)
                or previous.get("fixture") != args.fixture):
            raise SystemExit("Native terrain/shared lifecycle fresh repeat requires identical shaders and fixture")
        if args.gpu_terrain and previous.get("terrainInputReceipt", {}).get("sha256") != sha256(args.terrain_input_receipt):
            raise SystemExit("Native terrain fresh repeat requires the identical accepted input receipt")
        if previous["renderer"] != args.renderer or previous["buildReceipt"]["sha256"] != sha256(receipt_path):
            raise SystemExit("Fresh repeat requires the identical renderer build receipt")
    subprocess.run([sys.executable, str(root / "scripts/rendering/verify-software-reference.py")], check=True)
    reference = json.loads((root / "docs/vulkan-software-reference.json").read_text(encoding="utf-8"))
    frozen = root / reference["localReference"]
    licensed_assets = {}
    for installation, names in ((args.rct2_path, ("g1.dat",)), (args.rct1_path, ("csg1.dat", "csg1i.dat"))):
        if installation:
            for name in names:
                path = (installation / "Data" / name).resolve(strict=True)
                licensed_assets[name] = {"path": str(path), "sha256": sha256(path)}
    data = frozen / "package/data"
    park = (args.park or frozen / "testdata/parks/small_park_with_ferris_wheel.sv6").resolve(strict=True)
    if args.fixture == "transparent-history" and sha256(park) != "66ab45181c2c3f383b2674eb823e45447da05d055ca533c94e3107463264f3cd":
        raise SystemExit("Transparent history requires the unchanged accepted input02 park")
    terrain_input = None
    terrain_expected_tiles = None
    terrain_input_pins = {}
    if args.terrain_input_receipt:
        terrain_path = args.terrain_input_receipt.resolve(strict=True)
        terrain_input_pins[terrain_path] = sha256(terrain_path)
        terrain_receipt = json.loads(terrain_path.read_text(encoding="utf-8"))
        if (terrain_receipt.get("accepted") is not True or terrain_receipt.get("fixture") != "nonuniform-terrain-v1"
                or terrain_receipt.get("park", {}).get("sha256") != sha256(park)
                or sha256(park) != "66ab45181c2c3f383b2674eb823e45447da05d055ca533c94e3107463264f3cd"):
            raise SystemExit("Terrain UI admission requires the exact accepted input02 park")
        process = next((p for p in terrain_receipt.get("processes", []) if p.get("name") == "frozen-verify"), None)
        census_path = terrain_path.parent / "frozen-verify.json"
        if process is None or sha256(census_path) != process.get("reportSha256"):
            raise SystemExit("Accepted frozen terrain census changed")
        terrain_input_pins[census_path] = sha256(census_path)
        census = json.loads(census_path.read_text(encoding="utf-8"))["census"]
        if census.get("mapSize") != [32, 32] or any(census["entityTypeCounts"]):
            raise SystemExit("Terrain input census is outside the closed domain")
        terrain_expected_tiles = [[t["baseZ"],t["slope"],t["grass"],t["surfaceSlot"],t["edgeSlot"],
            2 if t["x"] in (0,31) or t["y"] in (0,31) else 1] for t in census["surfaces"]]
        if len(terrain_expected_tiles) != 1024:
            raise SystemExit("Terrain input does not contain 1024 raw surface facts")
        terrain_input = {"path":str(terrain_path),"sha256":terrain_input_pins[terrain_path],"parkSha256":sha256(park)}
    balloon_input = None
    expected_balloons = None
    if args.balloon_input_receipt:
        input_path = args.balloon_input_receipt.resolve(strict=True)
        receipt = json.loads(input_path.read_text(encoding="utf-8"))
        if receipt.get("accepted") is not True or receipt.get("fixture") not in ("balloon-static-v1", "balloon-static-v2"):
            raise SystemExit("Balloon input has not passed frozen/current immutable verification")
        if receipt.get("park", {}).get("sha256") != sha256(park):
            raise SystemExit("Balloon park differs from accepted immutable input")
        process = next((p for p in receipt.get("processes", []) if p.get("name") == "frozen-verify"), None)
        report_path = input_path.parent / "frozen-verify.json"
        if process is None or sha256(report_path) != process.get("reportSha256"):
            raise SystemExit("Accepted frozen balloon census changed")
        if args.gpu_balloons and receipt.get("fixture") != "balloon-static-v2":
            raise SystemExit("Native balloon qualification requires the corrected legitimate v2 fixture")
        expected_balloons = json.loads(report_path.read_text(encoding="utf-8"))["census"]["balloons"]
        if expected_balloons["count"] != args.require_balloon_count:
            raise SystemExit("Required balloon count differs from accepted census")
        balloon_input = {"path": str(input_path), "sha256": sha256(input_path), "censusSha256": sha256(report_path), "fixture": receipt["fixture"],
                         "legitimateAnimationFamily": receipt["fixture"] == "balloon-static-v2"}
    for reference_run, comparison_summary in references.items():
        if comparison_summary["parkSha256"] != sha256(park):
            raise SystemExit("Reference and candidate park inputs differ")
        if {name: item["sha256"] for name, item in comparison_summary["licensedAssets"].items()} != {
                name: item["sha256"] for name, item in licensed_assets.items()}:
            raise SystemExit("Reference and candidate licensed assets differ")
        for name, sample in comparison_summary["samples"].items():
            for filename, key in (("screen.rgba", "rgbaSha256"), ("screen.indexed", "indexedSha256"), ("report.json", "reportSha256")):
                path = reference_run / "captures" / name / filename
                if sha256(path) != sample[key]:
                    raise SystemExit("Reference capture changed: " + str(path))
    original_receipt = Path(build["sourceRoot"]) / "oracle-ui-source-receipt.json"
    oracle_receipt = None
    if original_receipt.is_file():
        original = json.loads(original_receipt.read_text(encoding="utf-8"))
        for name, digest in original["originalSourceSha256"].items():
            path = original_receipt.parent / name
            if not path.is_file() or sha256(path) != digest:
                raise SystemExit("Frozen UI oracle source changed: " + name)
        oracle_receipt = {"path": str(original_receipt), "sha256": sha256(original_receipt),
                          "originalFilesVerified": len(original["originalSourceSha256"])}
    shader_hashes = {}
    shader_receipt = None
    if args.renderer == "vulkan":
        if not args.shader_build_receipt:
            raise SystemExit("Vulkan capture requires --shader-build-receipt")
        shader_receipt_path = args.shader_build_receipt.resolve(strict=True)
        shader_build = json.loads(shader_receipt_path.read_text(encoding="utf-8"))
        if shader_build["status"] != "pass":
            raise SystemExit("Shader build receipt did not pass")
        for name, digest in shader_build["artifactSha256"].items():
            if name.endswith(".spv"):
                path = (root / name).resolve(strict=True)
                if root not in path.parents or sha256(path) != digest:
                    raise SystemExit("Shader binary changed: " + name)
                shader_hashes[name] = digest
        if not shader_hashes:
            raise SystemExit("Shader receipt has no compiled shaders")
        for name, digest in shader_build["sourceSha256"].items():
            if name.startswith("data/shaders/vulkan/") and build["sourceSha256"].get("source/" + name) != digest:
                raise SystemExit("UI and shader build source disagree: " + name)
        shader_receipt = {"path": str(shader_receipt_path), "sha256": sha256(shader_receipt_path)}
    output.mkdir(parents=True)
    if args.renderer == "vulkan":
        overlay = output / "data"
        overlay.mkdir()
        # Immutable asset inputs are linked individually; only shader binaries are overlaid.
        quote = lambda path: "'" + str(path).replace("'", "''") + "'"
        for item in data.iterdir():
            if item.name == "shaders":
                continue
            destination = overlay / item.name
            if item.is_dir():
                subprocess.run(["powershell", "-NoProfile", "-Command",
                                "New-Item -ItemType Junction -Path " + quote(destination)
                                + " -Target " + quote(item) + " | Out-Null"], check=True)
            else:
                shutil.copy2(item, destination)
        target = overlay / "shaders/vulkan"
        target.mkdir(parents=True)
        for name in shader_hashes:
            shutil.copy2(root / name, target / Path(name).name)
        data = overlay
    executable = build_root / "bin/ui-parity.exe"
    command = [str(executable), "--renderer", args.renderer, "--park", str(park), "--data", str(data),
               "--rct2", str(args.rct2_path.resolve(strict=True)), "--profile", str(output / "profile"),
               "--output", str(output / "captures"), "--width", str(args.width), "--height", str(args.height),
               "--rotation", str(args.rotation), "--zoom", str(args.zoom)]
    if args.shared_service_lifecycle:
        command += ["--shared-service-lifecycle", "true"]
    if args.window_scale != 1:
        command.extend(["--window-scale", format(args.window_scale, "g")])
    if args.rct1_path:
        command += ["--rct1", str(args.rct1_path.resolve(strict=True))]
    if args.fixture != "baseline":
        command += ["--fixture", args.fixture]
    if args.camera_mode != "explicit":
        command += ["--camera-mode", args.camera_mode]
    if args.landscape_smoothing != "true":
        command += ["--landscape-smoothing", args.landscape_smoothing]
    if args.require_world_surfaces != "any":
        command += ["--require-world-surfaces", args.require_world_surfaces]
    if args.paint_stable_sort == "true":
        command += ["--paint-stable-sort", "true"]
    if args.cherry_fixture:
        command += ["--cherry-fixture", args.cherry_fixture]
    if args.gpu_terrain:
        command += ["--gpu-terrain", "true"]
    if args.gpu_balloons:
        command += ["--gpu-balloons", "true"]
    if args.expect_gpu_balloon_fallback:
        command += ["--expect-gpu-balloon-fallback", "true"]
    if args.retained_balloons:
        command += ["--retained-balloons", "true"]
    if args.require_balloon_count is not None:
        command += ["--require-balloon-count", str(args.require_balloon_count)]
    if args.history_clear_zero:
        command += ["--history-clear-zero", "true"]
    if args.viewport_flags:
        command += ["--viewport-flags", str(args.viewport_flags)]
    for name in ("view_x", "view_y"):
        if getattr(args, name) is not None:
            command += ["--" + name.replace("_", "-"), str(getattr(args, name))]
    env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()}
    if args.shared_service_lifecycle or args.gpu_terrain or args.fixture in ("transparent-history", "world-dirty", "world-dirty-incremental") + MOTION_FAMILIES:
        for key in list(env):
            if key.startswith(("OPENRCT2_", "VK_")):
                env.pop(key)
    if args.validation:
        env.update(VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation", VK_LOADER_DEBUG="layer",
                   VK_LAYER_SETTINGS_PATH=str(output))
        (output / "vk_layer_settings.txt").write_text(
            "khronos_validation.validate_sync = true\n"
            "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n"
            "khronos_validation.log_filename = stdout\n"
            "khronos_validation.report_flags = error,warn\n"
            "khronos_validation.enable_message_limit = false\n", encoding="utf-8")
    lifecycle_pins = {}
    lifecycle_dlls = None
    if args.shared_service_lifecycle or args.gpu_terrain or args.fixture in ("transparent-history", "world-dirty", "world-dirty-incremental") + MOTION_FAMILIES:
        if any(build.get(key) for key in ("sourceChangesDuringBuild", "dependencyChangesDuringBuild", "missingArtifacts")):
            raise SystemExit("Shared lifecycle needs a stable qualified UI build")
        diagnostic_headers = ("test/ui-parity/UiFixtures.h", "test/ui-parity/UiParityMain.cpp") if args.fixture in ("world-dirty", "world-dirty-incremental") else ("test/ui-parity/UiParityMain.cpp",) if args.fixture == "transparent-history" or args.fixture in MOTION_FAMILIES else ("test/ui-parity/UiTerrainFixture.h", "test/ui-parity/UiParityMain.cpp") if args.gpu_terrain else (
            "test/ui-parity/UiSharedServiceLifecycle.h", "test/ui-parity/UiParityMain.cpp")
        if args.fixture in ("world-dirty", "world-dirty-incremental") + MOTION_FAMILIES:
            diagnostic_headers += ("test/ui-parity/UiTreeTrackSceneLocator.h",)
        if args.cherry_fixture:
            diagnostic_headers += ("test/ui-parity/UiCherryTrackFixture.h",)
        for relative in diagnostic_headers:
            path = root / relative
            if sha256(path) != build.get("sourceSha256", {}).get("harness/" + relative):
                raise SystemExit("UI lifecycle harness does not match the compiled receipt: " + relative)
            lifecycle_pins[path] = sha256(path)
        lifecycle_pins.update({renderer_header: renderer_header_digest,
                              receipt_path: sha256(receipt_path), Path(__file__).resolve(): sha256(Path(__file__)),
                              park: sha256(park)})
        if shader_receipt is not None:
            lifecycle_pins[shader_receipt_path] = sha256(shader_receipt_path)
        if args.validation:
            lifecycle_pins[output / "vk_layer_settings.txt"] = sha256(output / "vk_layer_settings.txt")
        for relative, digest in build["artifactSha256"].items():
            lifecycle_pins[build_root / relative] = digest
        lifecycle_dlls = {str(path): sha256(path) for path in sorted(executable.parent.glob("*.dll"))}
        if {Path(path).name: digest for path, digest in lifecycle_dlls.items()} != {
                Path(path).name: digest for path, digest in build.get("runtimeDllSha256", {}).items()}:
            raise SystemExit("Shared lifecycle runtime DLL membership differs from the build receipt")
        lifecycle_pins.update({Path(path): digest for path, digest in lifecycle_dlls.items()})
        for relative, digest in shader_hashes.items():
            lifecycle_pins[root / relative] = digest
            lifecycle_pins[data / "shaders/vulkan" / Path(relative).name] = digest
        for asset in licensed_assets.values():
            lifecycle_pins[Path(asset["path"])] = asset["sha256"]
        for reference_run, reference_summary in references.items():
            lifecycle_pins[reference_run / "summary.json"] = sha256(reference_run / "summary.json")
            if args.fixture in ("world-dirty", "world-dirty-incremental") + MOTION_FAMILIES:
                proof = reference_summary.get("sceneLocatorProof")
                if not isinstance(proof, dict) or not isinstance(proof.get("reportSha256"), str):
                    raise SystemExit("World reference lacks qualified tree/track scene census")
                lifecycle_pins[reference_run / "captures/tree-track-scenes.json"] = proof["reportSha256"]
            for name, sample in reference_summary["samples"].items():
                for filename, key in (("screen.rgba", "rgbaSha256"), ("screen.indexed", "indexedSha256"), ("report.json", "reportSha256")):
                    lifecycle_pins[reference_run / "captures" / name / filename] = sample[key]
        lifecycle_pins.update(terrain_input_pins)
        for path, digest in lifecycle_pins.items():
            if not path.is_file() or sha256(path) != digest:
                raise SystemExit("Shared lifecycle input changed before capture: " + str(path))
    with (output / "capture.log").open("w", encoding="utf-8") as log:
        try:
            result = subprocess.run(command, cwd=build_root / "bin", env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=300 if args.fixture in MOTION_FAMILIES else 120)
            exit_code = result.returncode
        except subprocess.TimeoutExpired:
            exit_code = "timeout"
    failures = []
    if exit_code != 0:
        failures.append("Capture executable failed: " + str(exit_code))
    lines = (output / "capture.log").read_text(encoding="utf-8", errors="replace").splitlines()
    failures += sorted(set(line for line in lines if "fallback images will be used" in line.lower()))
    if args.validation:
        failures += sorted(set(line for line in lines if any(marker in line.lower() for marker in
                               ("vuid-", "sync-hazard", "validation error", "validation warning", "validation performance warning"))))
        if not any("Insert instance layer" in line and "VK_LAYER_KHRONOS_validation" in line for line in lines):
            failures.append("Validation activation was not confirmed")
    comparisons = []
    scaling_checks = []
    samples = {}
    temporal_sequence = args.shared_service_lifecycle or args.fixture in ("light-night", "weather-precipitation", "weather-palette", "transparent-history") + FONT_FAMILIES + MOTION_FAMILIES
    required_names = (list(FIXTURE_STEPS[args.fixture]) if temporal_sequence else
                      [step + "-" + str(repetition) for repetition in range(2) for step in FIXTURE_STEPS[args.fixture]])
    if args.shared_service_lifecycle:
        required_names = list(VULKAN_ONLY_LIFECYCLE_PHASES if vulkan_only else LIFECYCLE_PHASES)
    for name in required_names:
        sample = output / "captures" / name
        raw = sample / "screen.rgba"
        indexed = sample / "screen.indexed"
        report = sample / "report.json"
        if not raw.is_file() or not indexed.is_file() or not report.is_file():
            failures.append("Missing required capture " + name)
            continue
        metadata = json.loads(report.read_text(encoding="utf-8"))
        if vulkan_only and (metadata.get("vulkanOnly") is not True or metadata.get("renderer") != "vulkan"):
            failures.append("Capture does not satisfy its pinned Vulkan-only contract " + name)
        elif not vulkan_only and metadata.get("vulkanOnly") is True:
            failures.append("Capture claims a Vulkan-only contract absent from its source manifest " + name)
        failures.extend(name + ": " + reason for reason in validate_camera_sample(metadata, args))
        if metadata.get("paintStableSort", False) is not (args.paint_stable_sort == "true"):
            failures.append("Actual paint stable-sort policy differs " + name)
        cherry = metadata.get("cherryFixture")
        if args.cherry_fixture:
            if (not isinstance(cherry, dict) or cherry.get("kind") != "everythingpark-adjacent-cherry-substitution"
                    or cherry.get("after", {}).get("id") != args.cherry_fixture
                    or cherry.get("treeTile") != [154, 210] or cherry.get("treeBaseZ") != 336
                    or cherry.get("track", {}).get("tile") != [155, 210]):
                failures.append("Missing or incorrect cherry substitution " + name)
        elif cherry is not None:
            failures.append("Unexpected world mutation in ordinary capture " + name)
        if args.renderer == "vulkan" and args.require_world_surfaces != "any":
            coverage = metadata.get("commandCoverage", {})
            expected_native = args.require_world_surfaces == "true"
            if coverage.get("worldSurfaces") is not expected_native:
                failures.append("Required world-surface admission mismatch " + name)
            if expected_native and (coverage.get("worldSurfaceRecordCount", 0) <= 0 or coverage.get("worldEpoch", 0) <= 0):
                failures.append("Native terrain records/epoch missing " + name)
        if args.require_balloon_count is not None:
            census = metadata.get("balloonState", {})
            if census.get("count") != args.require_balloon_count or len(census.get("records", [])) != args.require_balloon_count:
                failures.append("Missing positive balloon census " + name)
            if census != expected_balloons:
                failures.append("UI balloon state differs from accepted frozen reload " + name)
        if args.retained_balloons:
            publication = metadata.get("balloonPublication", {})
            metrics = publication.get("captureMetrics", {})
            if (publication.get("profile") != "retainedBalloons" or publication.get("epoch", 0) <= 0
                    or publication.get("sequence", 0) <= 0 or publication.get("count") != args.require_balloon_count
                    or publication.get("records") != metadata.get("balloonState", {}).get("records")
                    or publication.get("sourceTick") != metadata.get("simulationTicks")
                    or metrics.get("bulkCopiedBytes") != 0
                    or metrics.get("fallbackIndexedBalloons") != args.require_balloon_count
                    or publication.get("gpuAdmission") is not (args.gpu_balloons and not args.expect_gpu_balloon_fallback)):
                failures.append("Invalid named retained balloon publication " + name)
        if args.gpu_terrain:
            failures.extend(reason + " " + name for reason in validate_native_terrain_sample(
                metadata, terrain_expected_tiles, args.expect_gpu_terrain_fallback))
        if args.require_cpu_world_paint:
            cpu = metadata.get("cpuViewportPaint", {})
            if any(type(cpu.get(k)) is not int or cpu[k] <= 0 for k in ("generate", "arrange", "draw")):
                failures.append("CPU world paint counter control is not positive " + name)
        if args.gpu_balloons:
            failures.extend(reason + " " + name for reason in validate_native_balloon_sample(
                metadata, args.expect_gpu_balloon_fallback))
        extent = metadata["physicalExtent"]
        logical_extent = metadata["logicalExtent"]
        if raw.stat().st_size != extent[0] * extent[1] * 4:
            failures.append("Invalid capture size " + name)
            continue
        if indexed.stat().st_size != logical_extent[0] * logical_extent[1]:
            failures.append("Invalid indexed capture size " + name)
            continue
        samples[name] = {"rgbaSha256": sha256(raw), "indexedSha256": sha256(indexed),
                         "reportSha256": sha256(report), "metadata": metadata}
        if args.compare_unscaled_run:
            unscaled = args.compare_unscaled_run.resolve(strict=True) / "captures" / name
            unscaled_metadata = json.loads((unscaled / "report.json").read_text(encoding="utf-8"))
            scale_failures, scale_evidence = validate_scaled_font_sample(
                metadata, unscaled_metadata, raw.read_bytes(), indexed.read_bytes(),
                (unscaled / "screen.rgba").read_bytes(), (unscaled / "screen.indexed").read_bytes(),
                args.window_scale, args.width, args.height)
            failures.extend(name + ": " + reason for reason in scale_failures)
            scaling_checks.append({"capture": name, "reference": str(unscaled), **scale_evidence})
        if args.compare_run:
            reference_name = "baseline-0" if args.shared_service_lifecycle and not args.fresh_repeat else name
            comparison = args.compare_run.resolve(strict=True) / "captures" / reference_name
            reference_report = json.loads((comparison / "report.json").read_text(encoding="utf-8"))
            if reference_report.get("paintStableSort", False) is not (args.paint_stable_sort == "true"):
                failures.append("Reference paint stable-sort policy differs " + name)
            for key in ("fixture", "fixtureVersion", "logicalExtent", "physicalExtent", "viewPosition", "rotation", "zoom", "cameraMode", "assetState", "simulationTicks", "paletteEffectFrame", "landscapeSmoothing", "viewportFlags", "language", "themePreset", "windowScale", "scaleQuality", "inputState", "step", "repetition", "lighting", "weather", "fonts", "scaling", "balloonState", "transparentHistory", "cherryFixture"):
                if reference_report.get(key) != metadata.get(key):
                    failures.append("Fixture input mismatch " + name + ": " + key)
            if args.fixture in MOTION_FAMILIES:
                for key in ("schema", "initialTick", "tickOffset", "totalTicks", "pass", "positions", "vehicles",
                            "consumedVehicles", "consumedSourceTick"):
                    if reference_report.get("worldMotion", {}).get(key) != metadata.get("worldMotion", {}).get(key):
                        failures.append("World motion state mismatch " + name + ": " + key)
            expected = (comparison / "screen.rgba").read_bytes()
            actual = raw.read_bytes()
            if len(expected) != len(actual):
                failures.append("Comparison byte sizes differ " + name)
                continue
            differing = sum(expected[i:i + 4] != actual[i:i + 4] for i in range(0, len(actual), 4))
            pair = output / "comparisons" / name
            pair.mkdir(parents=True)
            ref_image = Image.frombytes("RGBA", tuple(extent), expected)
            candidate = Image.frombytes("RGBA", tuple(extent), actual)
            ref_image.save(pair / "reference.png")
            candidate.save(pair / "candidate.png")
            difference = ImageChops.difference(ref_image, candidate)
            # RGB view includes alpha-only differences as white, preserving inspectability.
            channels = difference.split()
            visible = Image.merge("RGB", tuple(ImageChops.lighter(channel, channels[3]) for channel in channels[:3]))
            visible.save(pair / "diff.png")
            visible.point(lambda value: min(255, value * 8)).save(pair / "diff-amplified.png")
            first = next((i // 4 for i in range(0, len(actual), 4) if expected[i:i + 4] != actual[i:i + 4]), None)
            comparisons.append({"capture": name, "differingPixels": differing, "boundsExclusive": visible.getbbox(),
                                "layer": "physical-rgba",
                                "firstMismatch": [first % extent[0], first // extent[0]] if first is not None else None,
                                "maxChannelError": [channel.getextrema()[1] for channel in channels],
                                "referenceSha256": hashlib.sha256(expected).hexdigest(), "candidateSha256": sha256(raw),
                                "manualVisualReview": "required for every divergence"})
            if differing:
                failures.append("Pixel divergence " + name + ": " + str(differing))
            expected_indices = (comparison / "screen.indexed").read_bytes()
            actual_indices = indexed.read_bytes()
            if len(expected_indices) != len(actual_indices):
                failures.append("Indexed comparison sizes differ " + name)
                continue
            index_differences = sum(a != b for a, b in zip(expected_indices, actual_indices))
            index_pair = pair / "indexed"
            index_pair.mkdir()
            index_reference = Image.frombytes("L", tuple(logical_extent), expected_indices)
            index_candidate = Image.frombytes("L", tuple(logical_extent), actual_indices)
            index_reference.save(index_pair / "reference.png")
            index_candidate.save(index_pair / "candidate.png")
            index_difference = ImageChops.difference(index_reference, index_candidate)
            index_difference.save(index_pair / "diff.png")
            index_difference.point(lambda value: 255 if value else 0).save(index_pair / "diff-mask.png")
            comparisons.append({"capture": name, "layer": "indexed", "differingPixels": index_differences,
                                "boundsExclusive": index_difference.getbbox(), "referenceSha256": hashlib.sha256(expected_indices).hexdigest(),
                                "candidateSha256": sha256(indexed), "manualVisualReview": "required for every divergence"})
            if index_differences:
                failures.append("Indexed pixel divergence " + name + ": " + str(index_differences))
    history_proof = None
    if args.fixture == "transparent-history":
        history_proof = {"schema":1,"transitions":[],"initialOpaque":samples.get(required_names[1],{}),"postLoad":samples.get(required_names[0],{})}
        for ordinal, name in enumerate(required_names, 1):
            sample = samples.get(name)
            if sample is None:
                failures.append("Missing transparent history phase " + name)
                continue
            metadata = sample["metadata"]
            history = metadata.get("transparentHistory",{})
            flags = 524288 if 4 <= ordinal <= 6 else 0
            if (history.get("schema") != 1 or history.get("phase") != name
                    or history.get("warmupViewportFlags") != 0 or history.get("warmupPaints") != 0
                    or history.get("paintOrdinal") != ordinal or history.get("viewportFlags") != flags
                    or history.get("priorOpaqueCaptures") != min(max(ordinal - 2,0),2)
                    or history.get("explicitClearZero") is not args.history_clear_zero
                    or history.get("painterCalled") is not (ordinal != 1)
                    or metadata.get("viewportFlags") != flags or metadata.get("forcedFullInvalidation") is not True
                    or metadata.get("cameraContract",{}).get("paintOrdinal") != ordinal
                    or metadata.get("simulationTicks") != samples.get(required_names[0],{}).get("metadata",{}).get("simulationTicks")):
                failures.append("Invalid transparent history phase contract " + name)
            if name in ("history-opaque-settled", "history-opaque-restored"):
                for layer in ("rgbaSha256","indexedSha256"):
                    if sample[layer] != samples.get(required_names[1],{}).get(layer):
                        failures.append("Opaque history baseline was not stable/restored " + name + " " + layer)
            if ordinal > 1 and required_names[ordinal - 2] in samples:
                prior = required_names[ordinal - 2]
                first = (output / "captures" / prior / "screen.indexed").read_bytes()
                second = (output / "captures" / name / "screen.indexed").read_bytes()
                if len(first) != 960*640 or len(second) != len(first):
                    failures.append("Transparent history indexed extent mismatch " + name)
                else:
                    transitions = {}
                    for old,new in zip(first,second):
                        if old != new:
                            key = str(old) + "->" + str(new)
                            transitions[key] = transitions.get(key,0) + 1
                    history_proof["transitions"].append({"from":prior,"to":name,
                        "differingPixels":sum(transitions.values()),"indexedTransitions":transitions,
                        "fromSha256":sha256(output / "captures" / prior / "screen.indexed"),
                        "toSha256":sha256(output / "captures" / name / "screen.indexed")})
    motion_proof = None
    if args.fixture in MOTION_FAMILIES:
        failures.extend(validate_world_motion_samples(samples, int(args.fixture.removeprefix("world-motion"))))
        motion_proof = {"schema":1,"phases":[{"name":name, **sample["metadata"].get("worldMotion", {}),
            "cpuColumns":sample["metadata"].get("cpuViewportPaint"),
            "indexedSha256":sample["indexedSha256"],"rgbaSha256":sample["rgbaSha256"]} for name,sample in samples.items()],
            "inputSha256":{str(p):d for p,d in lifecycle_pins.items()}}
        motion_proof["divergences"] = []
        for tick in range(int(args.fixture.removeprefix("world-motion")) + 1):
            first_name, second_name = "motion-" + str(tick) + "-damage", "motion-" + str(tick) + "-full"
            if first_name not in samples or second_name not in samples:
                continue
            for key, filename, mode, extent_key in (("indexedSha256", "screen.indexed", "L", "logicalExtent"),
                                                    ("rgbaSha256", "screen.rgba", "RGBA", "physicalExtent")):
                if samples[first_name][key] == samples[second_name][key]:
                    continue
                extent = tuple(samples[first_name]["metadata"][extent_key])
                first = Image.frombytes(mode, extent, (output / "captures" / first_name / filename).read_bytes())
                second = Image.frombytes(mode, extent, (output / "captures" / second_name / filename).read_bytes())
                difference = ImageChops.difference(first, second)
                if mode == "RGBA":
                    parts = difference.split()
                    difference = Image.merge("RGB", tuple(ImageChops.lighter(part, parts[3]) for part in parts[:3]))
                path = output / "world-motion-comparisons" / str(tick) / key
                path.mkdir(parents=True)
                first.save(path / "damage.png")
                second.save(path / "full.png")
                difference.save(path / "diff.png")
                motion_proof["divergences"].append({"tickOffset":tick,"layer":key,
                    "boundsExclusive":difference.getbbox(),"path":str(path),"manualVisualReview":"required"})
    scene_locator_proof = None
    if args.fixture in ("world-dirty", "world-dirty-incremental") + MOTION_FAMILIES:
        census_path = output / "captures/tree-track-scenes.json"
        try:
            census = json.loads(census_path.read_text(encoding="utf-8"))
            failures.extend(validate_tree_track_census(census, samples, args.fixture in MOTION_FAMILIES))
            for reference_run in references:
                reference_census = json.loads((reference_run / "captures/tree-track-scenes.json").read_text(encoding="utf-8"))
                if reference_census != census:
                    failures.append("Tree/track scene census differs from reference " + str(reference_run))
            scene_locator_proof = {"reportSha256":sha256(census_path), "simulationTicks":census.get("simulationTicks"),
                "viewportExtent":census.get("viewportExtent"), "treeCounts":census.get("treeCounts"),
                "woodenTrackElements":census.get("woodenTrackElements"), "matchedTrees":census.get("matchedTrees"),
                "unresolvedScenery":census.get("unresolvedScenery"), "candidateCount":len(census.get("candidates", [])),
                "candidateScope":"Semantic proximity; actual occlusion requires manual image inspection"}
        except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
            failures.append("Missing/malformed tree-track scene census: " + str(error))
    world_dirty_proof = None
    if args.fixture in ("world-dirty", "world-dirty-incremental"):
        failures.extend(validate_world_dirty_samples(samples, args.fixture.endswith("-incremental")))
        world_dirty_proof = {"schema":1,"scope":"Requested invalidation rectangles; ordinary backend may expand them",
                             "phases":[],"inputSha256":{str(p):d for p,d in lifecycle_pins.items()}}
        for name in required_names:
            if name not in samples:
                continue
            sample = samples[name]
            metadata = sample["metadata"]
            world_dirty_proof["phases"].append({"name":name,
                "requested":metadata.get("inputState",{}).get("detail",{}).get("worldDirty"),
                "paintCounter":metadata.get("worldDirtyDrawCount"),"cpuColumns":metadata.get("cpuViewportPaint"),
                "forcedFullInvalidation":metadata.get("forcedFullInvalidation"),
                "indexedSha256":sample["indexedSha256"],"rgbaSha256":sample["rgbaSha256"]})
            baseline = "dirty-baseline-" + name.rsplit("-",1)[1]
            if baseline not in samples or name == baseline:
                continue
            for filename, mode, layer in (("screen.indexed","L","indexed"),("screen.rgba","RGBA","rgba")):
                extent = metadata["logicalExtent" if mode == "L" else "physicalExtent"]
                before = (output / "captures" / baseline / filename).read_bytes()
                after = (output / "captures" / name / filename).read_bytes()
                if before == after:
                    continue
                channels = 1 if mode == "L" else 4
                if len(before) != len(after) or len(after) != extent[0]*extent[1]*channels:
                    failures.append("Dirty-world comparison buffer size mismatch " + name)
                    continue
                first, second = Image.frombytes(mode, tuple(extent), before), Image.frombytes(mode, tuple(extent), after)
                diff = ImageChops.difference(first, second)
                if mode == "RGBA":
                    parts = diff.split()
                    diff = Image.merge("RGB",tuple(ImageChops.lighter(part,parts[3]) for part in parts[:3]))
                pair = output / "world-dirty-comparisons" / name / layer
                pair.mkdir(parents=True)
                first.save(pair / "reference.png")
                second.save(pair / "candidate.png")
                diff.save(pair / "diff.png")
                diff.point(lambda v:min(255,v*8)).save(pair / "diff-amplified.png")
                comparisons.append({"capture":name,"comparison":"unchanged-world-invalidation","layer":layer,
                    "differingPixels":sum(before[i:i+channels] != after[i:i+channels] for i in range(0,len(after),channels)),
                    "boundsExclusive":diff.getbbox(),"manualVisualReview":"required for every divergence"})
    for step in (() if temporal_sequence else FIXTURE_STEPS[args.fixture]):
        first, second = samples.get(step + "-0"), samples.get(step + "-1")
        if first and second:
            for layer in ("rgbaSha256", "indexedSha256"):
                if first[layer] != second[layer]:
                    failures.append("Repeated capture differs " + step + ": " + layer)
    # These are semantic checks within one controlled weather sequence. They
    # cannot be replaced by comparing consecutive paint ordinals as repeats.
    weather_equalities = {
        "weather-precipitation": (("weather-clear", "weather-cleared"),
                                  ("weather-rain-heavy", "weather-rain-restored")),
        "weather-palette": (("palette-clear", "palette-restored"),
                            ("palette-gloom-2", "palette-lightning-recovered")),
    }
    for first_name, second_name in weather_equalities.get(args.fixture, ()):
        if first_name in samples and second_name in samples:
            for layer in ("rgbaSha256", "indexedSha256"):
                if samples[first_name][layer] != samples[second_name][layer]:
                    failures.append("Weather restoration differs " + first_name + " / " + second_name + ": " + layer)
    weather_changes = {
        "weather-precipitation": (("weather-clear", "weather-rain-light"),
                                  ("weather-rain-light", "weather-rain-heavy"),
                                  ("weather-clear", "weather-snow-light"),
                                  ("weather-snow-light", "weather-snow-heavy")),
        "weather-palette": (("palette-clear", "palette-gloom-1"),
                            ("palette-gloom-1", "palette-gloom-2")),
    }
    for first_name, second_name in weather_changes.get(args.fixture, ()):
        if first_name in samples and second_name in samples:
            for layer in ("rgbaSha256", "indexedSha256"):
                if samples[first_name][layer] == samples[second_name][layer]:
                    failures.append("Weather effect absent " + first_name + " / " + second_name + ": " + layer)
    if args.fixture == "weather-palette" and "palette-lightning" in samples and "palette-gloom-2" in samples:
        flash, gloom = samples["palette-lightning"], samples["palette-gloom-2"]
        if flash["indexedSha256"] != gloom["indexedSha256"] or flash["rgbaSha256"] == gloom["rgbaSha256"]:
            failures.append("Lightning must change physical colours while preserving indices")
    if args.fixture in FONT_FAMILIES:
        failures.extend(validate_font_samples(args.fixture, args.renderer, samples))
    if args.compare_full_run:
        base_family = args.fixture.removesuffix("-incremental")
        for name, sample in samples.items():
            reference_dir = args.compare_full_run / "captures" / name
            reference_report = json.loads((reference_dir / "report.json").read_text(encoding="utf-8"))
            if reference_report.get("paintStableSort", False) is not (args.paint_stable_sort == "true"):
                failures.append("Reference paint stable-sort policy differs " + name)
            if reference_report.get("fixture") != "main-ui-" + base_family:
                failures.append("Full-state reference family mismatch " + name)
            metadata = sample["metadata"]
            for key in ("fixtureVersion", "logicalExtent", "physicalExtent", "viewPosition", "rotation", "zoom", "simulationTicks", "paletteEffectFrame", "landscapeSmoothing", "viewportFlags", "language", "themePreset", "windowScale", "scaleQuality", "step", "repetition", "balloonState"):
                if reference_report.get(key) != metadata.get(key):
                    failures.append("Full-state input mismatch " + name + ": " + key)
            full_input = dict(reference_report.get("inputState", {}))
            incremental_input = dict(metadata.get("inputState", {}))
            full_input.pop("invalidation", None)
            incremental_input.pop("invalidation", None)
            if full_input != incremental_input:
                failures.append("Full-state window/input mismatch " + name)
            for filename, mode, extent_key, layer in (("screen.rgba", "RGBA", "physicalExtent", "physical-rgba"),
                                                      ("screen.indexed", "L", "logicalExtent", "indexed")):
                expected = (reference_dir / filename).read_bytes()
                actual = (output / "captures" / name / filename).read_bytes()
                channels = 4 if mode == "RGBA" else 1
                if len(expected) != len(actual):
                    failures.append("Full-state byte size mismatch " + name + ": " + layer)
                    continue
                differing = sum(expected[i:i + channels] != actual[i:i + channels] for i in range(0, len(actual), channels))
                extent = tuple(metadata[extent_key])
                reference_image = Image.frombytes(mode, extent, expected)
                candidate = Image.frombytes(mode, extent, actual)
                difference = ImageChops.difference(reference_image, candidate)
                if mode == "RGBA":
                    parts = difference.split()
                    difference = Image.merge("RGB", tuple(ImageChops.lighter(part, parts[3]) for part in parts[:3]))
                pair = output / "full-state-comparisons" / name / layer
                pair.mkdir(parents=True)
                reference_image.save(pair / "reference.png")
                candidate.save(pair / "candidate.png")
                difference.save(pair / "diff.png")
                difference.point(lambda value: min(255, value * 8)).save(pair / "diff-amplified.png")
                comparisons.append({"capture": name, "comparison": "full-invalidation-state", "layer": layer,
                                    "differingPixels": differing, "boundsExclusive": difference.getbbox(),
                                    "referenceSha256": hashlib.sha256(expected).hexdigest(),
                                    "candidateSha256": hashlib.sha256(actual).hexdigest(),
                                    "manualVisualReview": "required for every divergence"})
                if differing:
                    failures.append("Incremental/full-state divergence " + name + ": " + layer + ": " + str(differing))
    if args.fixture != "baseline" and not temporal_sequence:
        for repetition in range(2):
            initial, restored = samples.get("empty-ui-" + str(repetition)), samples.get("restored-" + str(repetition))
            if initial and restored:
                for layer in ("rgbaSha256", "indexedSha256"):
                    if initial[layer] != restored[layer]:
                        failures.append("Restored baseline differs repetition " + str(repetition) + ": " + layer)
    lifecycle_proof = None
    if args.shared_service_lifecycle:
        report_path = output / "captures/shared-service-lifecycle.json"
        index_path = output / "captures/lifecycle-aux.indexed"
        rgba_path = output / "captures/lifecycle-aux.rgba"
        try:
            lifecycle_report = json.loads(report_path.read_text(encoding="utf-8"))
            auxiliary_indices, auxiliary_rgba = index_path.read_bytes(), rgba_path.read_bytes()
            failures.extend(validate_shared_lifecycle(lifecycle_report, samples, auxiliary_indices, auxiliary_rgba, vulkan_only))
            Image.frombytes("RGBA", (64, 64), auxiliary_rgba).save(output / "captures/lifecycle-aux.png")
            lifecycle_proof = {"report": lifecycle_report, "reportSha256": sha256(report_path),
                               "auxIndexedSha256": sha256(index_path), "auxRgbaSha256": sha256(rgba_path),
                               "identityScope": "Injected production owner observed between completed phases; no interception of VkDevice creation",
                               "shutdownScope": "Weak owner/device expiration and SDL video stopped after Context destruction; no timestamped destruction-order trace"}
        except (OSError, ValueError, TypeError, KeyError) as error:
            failures.append("Missing/malformed shared lifecycle evidence: " + str(error))
        changed = [str(path) for path, digest in lifecycle_pins.items() if not path.is_file() or sha256(path) != digest]
        if {str(path): sha256(path) for path in sorted(executable.parent.glob("*.dll"))} != lifecycle_dlls:
            changed.append("runtime DLL membership")
        if changed:
            failures.append("Shared lifecycle inputs changed during capture: " + "; ".join(changed))
        if lifecycle_proof is not None:
            lifecycle_proof["inputSha256"] = {str(path): digest for path, digest in lifecycle_pins.items()}
            lifecycle_proof["inputsChangedDuringRun"] = changed
    if args.gpu_terrain and not args.expect_gpu_terrain_fallback:
        failures.extend(validate_terrain_preparation_sequence(
            [samples[name]["metadata"] for name in required_names if name in samples]))
    terrain_changed = []
    if args.gpu_terrain or args.fixture in ("transparent-history", "world-dirty", "world-dirty-incremental") + MOTION_FAMILIES:
        terrain_changed = [str(p) for p,d in lifecycle_pins.items() if not p.is_file() or sha256(p) != d]
        if {str(p):sha256(p) for p in sorted(executable.parent.glob("*.dll"))} != lifecycle_dlls:
            terrain_changed.append("runtime DLL membership")
        if terrain_changed:
            failures.append("Terrain diagnostic inputs changed: " + "; ".join(terrain_changed))
    summary = {"schema": 1, "status": "fail" if failures else "pass", "failures": failures,
               "rendererContract": renderer_contract,
               "sharedServiceLifecycle": args.shared_service_lifecycle, "sharedServiceProof": lifecycle_proof,
               "cherryFixture": args.cherry_fixture,
               "transparentHistoryProof": history_proof, "historyClearZero": args.history_clear_zero,
                "worldDirtyProof":world_dirty_proof, "worldMotionProof":motion_proof,
                "sceneLocatorProof":scene_locator_proof,
               "transparentHistoryInputSha256": {str(p):d for p,d in lifecycle_pins.items()} if args.fixture == "transparent-history" else {},
               "command": command, "exitCode": exit_code, "renderer": args.renderer,
               "fixture": args.fixture, "requiredCaptures": required_names,
               "paintStableSort": args.paint_stable_sort == "true",
               "gpuTerrain": args.gpu_terrain, "expectGpuTerrainFallback": args.expect_gpu_terrain_fallback,
               "terrainInputReceipt": terrain_input, "requireCpuWorldPaint": args.require_cpu_world_paint,
               "terrainInputSha256": {str(p):d for p,d in lifecycle_pins.items()} if args.gpu_terrain else {},
               "terrainInputsChangedDuringRun": terrain_changed,
               "gpuBalloons": args.gpu_balloons, "expectGpuBalloonFallback": args.expect_gpu_balloon_fallback,
               "retainedBalloons": args.retained_balloons, "requiredBalloonCount": args.require_balloon_count,
               "balloonInputReceipt": balloon_input,
               "repeatScope": ("fresh process against identical renderer build" if args.fresh_repeat else
                               "paint ordinals only; fresh-process repeat still required" if temporal_sequence else
                               "two full sequences within one process"),
               "comparisonReferences": [{"path": str(path), "summarySha256": sha256(path / "summary.json")}
                                        for path in references],
               "buildReceipt": {"path": str(receipt_path), "sha256": sha256(receipt_path)},
               "oracleSourceReceipt": oracle_receipt, "shaderBuildReceipt": shader_receipt,
               "shaderSha256": shader_hashes, "parkSha256": sha256(park),
               "licensedAssets": licensed_assets,
               "referenceReceiptSha256": sha256(root / "docs/vulkan-software-reference.json"),
               "samples": samples, "comparisons": comparisons}
    if args.window_scale != 1:
        summary["scalingChecks"] = scaling_checks
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": summary["status"], "failures": failures, "captures": len(samples),
                      "comparisons": len(comparisons),
                      "divergences": [item for item in comparisons if item["differingPixels"]],
                      "evidence": str(output / "summary.json")}))
    raise SystemExit(1 if failures else 0)


if __name__ == "__main__":
    main()
