"""Acceptance must count presented GPU frames rather than CPU paint attempts."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest
import tempfile
from PIL import Image

spec = importlib.util.spec_from_file_location('render_performance', Path(__file__).with_name('run-render-performance.py'))
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class PresentationEvidenceTest(unittest.TestCase):
    def payload(self, frames=1440):
        counters = dict(publishedVisualPackets=1440, supersededVisualPackets=1440 - frames,
                        unavailableVisualPackets=0, discardedVisualPackets=0,
                        visualFrameSubmissions=frames, presentRequests=frames, presentAccepted=frames,
                        presentOutOfDate=0, fenceCompletedFrames=frames, lostTimingSamples=0)
        return dict(schema=1, available=True, displayedFramesAvailable=False, elapsedSeconds=10.0,
                    drawAttempts=1440, intervalEnd=counters, afterDrain=copy.deepcopy(counters),
                    intervalSubmissionFPS=frames / 10, intervalAcceptedPresentFPS=frames / 10,
                    drainedSubmissionFPS=frames / 10, drainedAcceptedPresentFPS=frames / 10)

    def test_cpu_144_does_not_qualify_gpu_105(self):
        payload = self.payload(1050)
        report.validate_frame_presentation(payload, dict(elapsedSeconds=10.0, draws=dict(count=1440)))
        with self.assertRaisesRegex(ValueError, 'below'):
            report.require_present_rate(dict(framePresentation=payload), 143)

    def test_real_present_rate_qualifies_without_claiming_scanout(self):
        payload = self.payload()
        report.validate_frame_presentation(payload, dict(elapsedSeconds=10.0, draws=dict(count=1440)))
        report.require_present_rate(dict(framePresentation=payload), 143)
        payload['displayedFramesAvailable'] = True
        with self.assertRaisesRegex(ValueError, 'scanout'):
            report.validate_frame_presentation(payload, dict(elapsedSeconds=10.0, draws=dict(count=1440)))

    def test_legacy_draw_only_report_cannot_pass_present_gate(self):
        with self.assertRaisesRegex(ValueError, 'evidence'):
            report.require_present_rate(dict(metrics=dict(draws=dict(fps=144))), 143)

    def test_late_drain_does_not_inflate_measured_rate(self):
        payload = self.payload(1400)
        for key in ('visualFrameSubmissions', 'presentRequests', 'presentAccepted', 'fenceCompletedFrames'):
            payload['afterDrain'][key] = 1440
        payload['drainedSubmissionFPS'] = payload['drainedAcceptedPresentFPS'] = 144
        report.validate_frame_presentation(payload, dict(elapsedSeconds=10.0, draws=dict(count=1440)))
        with self.assertRaisesRegex(ValueError, 'below'):
            report.require_present_rate(dict(framePresentation=payload), 143)

    def test_completion_cannot_exceed_submission(self):
        payload = self.payload()
        payload['afterDrain']['fenceCompletedFrames'] += 1
        with self.assertRaisesRegex(ValueError, 'accounting'):
            report.validate_frame_presentation(payload, dict(elapsedSeconds=10.0, draws=dict(count=1440)))

    def pacing_payload(self):
        payload = self.payload()
        payload['acceptedPresentIntervals'] = dict(
            available=True, cohortTimestampSamples=1440, cohortAcceptedPresents=1440,
            timestampSamples=1439, intervals=1438, outOfOrderSamples=0,
            binWidthMicroseconds=100, percentilesAreUpperBounds=True,
            overflowIntervals=0, p50Ms=7.0, p95Ms=7.1, p99Ms=7.2, maxMs=7.16)
        return payload

    def test_histogram_upper_bound_can_exceed_exact_max_by_one_bin(self):
        report.validate_frame_presentation(
            self.pacing_payload(), dict(elapsedSeconds=10.0, draws=dict(count=1440)))

    def test_missing_or_unordered_present_timestamps_fail_pacing_evidence(self):
        for key, value in (('cohortTimestampSamples', 1439), ('intervals', 1439),
                           ('outOfOrderSamples', 1)):
            with self.subTest(key=key):
                payload = self.pacing_payload()
                payload['acceptedPresentIntervals'][key] = value
                with self.assertRaisesRegex(ValueError, 'cohort'):
                    report.validate_frame_presentation(payload, dict(elapsedSeconds=10.0, draws=dict(count=1440)))

    def test_invalid_present_interval_distribution_is_rejected(self):
        for key, value in (('maxMs', float('nan')), ('p99Ms', 8.0), ('p95Ms', 6.0)):
            with self.subTest(key=key):
                payload = self.pacing_payload()
                payload['acceptedPresentIntervals'][key] = value
                with self.assertRaisesRegex(ValueError, 'interval'):
                    report.validate_frame_presentation(payload, dict(elapsedSeconds=10.0, draws=dict(count=1440)))


class WorldStageEvidenceTest(unittest.TestCase):
    def test_physical_depth_profile_has_no_ordering_stages(self):
        payload = dict(schema=2, supported=True, samples=120, unavailable=0, discarded=0, pending=0,
                       stages={name: dict(meanUs=10, maxUs=20) for name in ('materialize', 'raster')})
        self.assertEqual(report.parse_world_gpu_profile('VULKAN_WORLD_PROFILE ' + json.dumps(payload)), [payload])
        payload['stages']['arrangeEmit'] = dict(meanUs=0, maxUs=0)
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            report.parse_world_gpu_profile('VULKAN_WORLD_PROFILE ' + json.dumps(payload))

    def test_bounds_coverage_reconciles_cache_and_fallback(self):
        payload = dict(schema=1, samples=2, nodes=1500, cachedNodes=1499, fallbackNodes=1,
                       comparisons=12000, maxColumnNodes=700, columns=4, activeColumns=3,
                       columnsOver512=2, saturatedSamples=0)
        self.assertEqual(report.parse_world_bounds_profile('VULKAN_WORLD_BOUNDS_PROFILE ' + json.dumps(payload)), [payload])
        payload['cachedNodes'] += 1
        with self.assertRaisesRegex(ValueError, 'accounting'):
            report.parse_world_bounds_profile('VULKAN_WORLD_BOUNDS_PROFILE ' + json.dumps(payload))

    def test_lifetime_summary_after_terminal_report_is_preserved(self):
        payload = dict(schema=1, supported=True, samples=120, unavailable=0, discarded=0, pending=1,
                       stages={name: dict(meanUs=10, maxUs=20) for name in
                               ('materialize', 'columnCount', 'columnPrefix', 'arrangeEmit', 'finalize', 'raster')})
        text = 'Integrated UI benchmark:\n...\nVULKAN_WORLD_PROFILE ' + json.dumps(payload) + '\n'
        self.assertEqual(report.parse_world_gpu_profile(text), [payload])
        payload['stages']['arrangeEmit']['meanUs'] = float('nan')
        with self.assertRaisesRegex(ValueError, 'duration'):
            report.parse_world_gpu_profile('VULKAN_WORLD_PROFILE ' + json.dumps(payload))



class ScreenshotEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.profile = self.root / 'profile'
        self.screenshots = self.profile / 'screenshot'
        self.screenshots.mkdir(parents=True)
        self.output = self.root / 'review'
        self.output.mkdir()
        self.result = dict(states=dict(final=dict(simulationTick=dict(tick=3000))), finalEntityChecksum='abc')
        path = self.png('final.png')
        self.receipt = dict(schema=1, outsideMeasurement=True, authoritativeStateUnchanged=True,
                            completedFrameNumber=4, simulationTick=3000, entityChecksum='abc', partialRender=False,
                            logicalExtent=[64, 96], drawableExtent=[64, 96], path=str(path),
                            camera=dict(viewPosition=[0, 0], rotation=0, zoom=0, flags=0))

    def png(self, name, size=(64, 96), mode='P', directory=None):
        path = (directory or self.screenshots) / name
        Image.new(mode, size).save(path, format='PNG')
        return path

    def capture(self, path, **overrides):
        record = dict(path=str(path), step=5, tick=128, viewPosition=[0, 0], zoom=2)
        record.update(overrides)
        return 'Camera stress capture v1: ' + json.dumps(record) + '\n'

    def qualify(self, extra='', camera=False):
        text = extra + 'Final benchmark screenshot v1: ' + json.dumps(self.receipt) + '\n'
        return report.qualify_final_screenshot(text, self.profile, self.output, self.result, 64, 96, camera)

    def test_ordinary_single_final_receipt_remains_valid(self):
        self.assertEqual(self.qualify()['receipt'], self.receipt)

    def test_declared_startup_and_camera_receipts_allow_only_their_images(self):
        startup = self.png('startup.png')
        camera = self.png('tour.png')
        extra = 'Vulkan startup: loading UI capture: ' + str(startup) + '\n' + self.capture(camera)
        self.assertEqual(self.qualify(extra, camera=True)['receipt'], self.receipt)

    def test_ordinary_benchmark_rejects_camera_receipts(self):
        with self.assertRaisesRegex(ValueError, 'not ordinary'):
            self.qualify(self.capture(self.png('tour.png')))

    def test_undeclared_png_is_rejected_even_during_camera_stress(self):
        self.png('unclaimed.PNG')
        with self.assertRaisesRegex(ValueError, 'Unexpected PNG'):
            self.qualify(camera=True)

    def test_declared_path_cannot_escape_profile(self):
        with self.assertRaisesRegex(ValueError, 'escaped'):
            self.qualify(self.capture(self.png('outside.png', directory=self.root)), camera=True)

    def test_declared_capture_must_be_indexed_png_of_requested_extent(self):
        for name, size, mode in [('wrong-size.png', (32, 96), 'P'), ('rgb.png', (64, 96), 'RGB')]:
            path = self.png(name, size, mode)
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'indexed main canvas'):
                self.qualify(self.capture(path), camera=True)
            path.unlink()

    def test_renamed_non_png_is_rejected(self):
        path = self.screenshots / 'pretend.png'
        Image.new('RGB', (64, 96)).save(path, format='BMP')
        with self.assertRaisesRegex(ValueError, 'indexed main canvas'):
            self.qualify(self.capture(path), camera=True)

    def test_repeated_receipts_cannot_alias_the_final_frame(self):
        with self.assertRaisesRegex(ValueError, 'reused final'):
            self.qualify(self.capture(Path(self.receipt['path'])), camera=True)

    def test_camera_receipt_fields_and_duplicate_steps_are_checked(self):
        path = self.png('tour.png')
        for override in [dict(step=True), dict(tick=-1), dict(zoom=4), dict(viewPosition=[0]), dict(extra=1)]:
            with self.subTest(override=override), self.assertRaisesRegex(ValueError, 'receipt'):
                self.qualify(self.capture(path, **override), camera=True)
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.qualify(self.capture(path) * 2, camera=True)

    def test_pacing_is_explicit_and_ordinary_default_unchanged(self):
        self.assertEqual(report.expected_simulation_pacing(), 'ordinary Turbo 360 TPS target')
        self.assertEqual(report.expected_simulation_pacing(uncapped=True), 'uncapped headroom')
        self.assertEqual(report.expected_simulation_pacing(camera_stress=True), 'normal speed camera stress')


class SimulationAttributionEvidenceTest(unittest.TestCase):
    def payload(self):
        names = ('tickPrelude', 'logicPrelude', 'map', 'routes', 'peeps', 'restoreProvisional',
                 'vehicles', 'miscEntities', 'rides', 'park', 'researchRatings', 'newsAnimations',
                 'spatialIndex', 'actionsNetworkScripts', 'tickTail')
        event = dict(simulationTick=3134427, offsetMs=1400.0, wallMs=58.0,
                     threadCycles=6472492, threadCyclesAvailable=True)
        phase = dict(count=1, wallMs=58.0, threadCycles=6472492, threadCycleSamples=1, worst=[event])
        worker = dict(count=32, grain=4, submittedWorkers=2, submitMs=0.1, callerMs=1.0,
                      retireMs=0.1, waitMs=56.8, callerCycles=2200000, callerCyclesAvailable=True,
                      waitCycles=8000, waitCyclesAvailable=True, completedWorkers=1, retiredWorkers=1,
                      remainingAtWait=1, workerTotalWorkMs=2.0, workerTotalCycles=6400000,
                      workerCycleSamples=1, workerMaxWorkMs=2.0, workerMaxWorkCycles=6400000,
                      workerMaxWorkCyclesAvailable=True, workerMaxStartDelayMs=0.5,
                      workerMaxCompletionLockMs=0.001, waitAcquireMutexMs=0.001,
                      conditionWaitMs=56.7, readyToResumeMs=55.0)
        parallel = copy.deepcopy(phase)
        parallel['worst'][0]['parallel'] = worker
        return dict(schema=1, capacityPerPhase=16, phases={name: copy.deepcopy(phase) for name in names},
                    parallel={name: copy.deepcopy(parallel) for name in ('peeps', 'vehicles')},
                    scope='Synthetic parser fixture; not measured evidence')

    def parse(self, payload):
        return report.parse_simulation_attribution(
            'Benchmark simulation attribution v1: ' + json.dumps(payload) + '\n')

    def test_optional_absent_report_preserves_legacy_clean_logs(self):
        self.assertIsNone(report.parse_simulation_attribution('Integrated UI benchmark:\nclean legacy output\n'))

    def test_complete_opt_in_report_preserves_wait_and_worker_evidence(self):
        payload = self.payload()
        self.assertEqual(self.parse(payload), payload)
        observed = self.parse(payload)['parallel']['vehicles']['worst'][0]['parallel']
        self.assertEqual(observed['completedWorkers'] + observed['retiredWorkers'], observed['submittedWorkers'])
        self.assertEqual(observed['readyToResumeMs'], 55.0)

    def test_duplicate_or_truncated_report_does_not_supply_complete_evidence(self):
        line = 'Benchmark simulation attribution v1: ' + json.dumps(self.payload()) + '\n'
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            report.parse_simulation_attribution(line + line)
        # A truncated object must fail rather than silently become valid evidence.
        with self.assertRaises(ValueError):
            report.parse_simulation_attribution(line[:-2])

    def test_missing_named_phase_or_parallel_seam_is_rejected(self):
        for container, key in (('phases', 'vehicles'), ('parallel', 'peeps')):
            payload = self.payload()
            del payload[container][key]
            with self.subTest(container=container), self.assertRaisesRegex(ValueError, 'schema'):
                self.parse(payload)

    def test_top16_requires_complete_sorted_bounded_event_set(self):
        for invalid in ('missing', 'seventeen', 'unsorted'):
            payload = self.payload()
            sample = payload['phases']['peeps']
            sample['count'] = 20
            sample['worst'] = [dict(sample['worst'][0], wallMs=float(30-i)) for i in range(16)]
            if invalid == 'missing':
                sample['worst'].pop()
            elif invalid == 'seventeen':
                sample['worst'].append(copy.deepcopy(sample['worst'][-1]))
            else:
                sample['worst'][1]['wallMs'] = 31
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                self.parse(payload)

    def test_worker_retirement_and_duration_partition_cannot_be_fabricated(self):
        for key, value in (('retiredWorkers', 2), ('remainingAtWait', 2),
                           ('workerCycleSamples', 2), ('readyToResumeMs', 57.0),
                           ('callerMs', 2.0), ('workerMaxWorkMs', float('nan'))):
            payload = self.payload()
            payload['parallel']['peeps']['worst'][0]['parallel'][key] = value
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'ParallelFor'):
                self.parse(payload)

    def test_invalid_phase_cycles_tick_duration_and_schema_are_rejected(self):
        for key, value in (('threadCycles', True), ('simulationTick', 0x100000000),
                           ('threadCyclesAvailable', 1), ('wallMs', -1), ('offsetMs', float('inf'))):
            payload = self.payload()
            payload['phases']['vehicles']['worst'][0][key] = value
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'event'):
                self.parse(payload)
        payload = self.payload()
        payload['schema'] = 2
        with self.assertRaisesRegex(ValueError, 'schema'):
            self.parse(payload)

    def test_empty_measurement_does_not_qualify_enabled_attribution(self):
        payload = self.payload()
        payload['phases']['peeps'] = dict(count=0, wallMs=0, threadCycles=0, threadCycleSamples=0, worst=[])
        with self.assertRaisesRegex(ValueError, 'did not measure'):
            self.parse(payload)


if __name__ == '__main__':
    unittest.main()
