"""Acceptance must count presented GPU frames rather than CPU paint attempts."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

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


if __name__ == '__main__':
    unittest.main()
