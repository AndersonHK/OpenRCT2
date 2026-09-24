"""Focused authoring regressions; no game, C++ painter, compiler or GPU execution."""
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    'native_track_extractor', Path(__file__).with_name('extract-native-track-recipes.py'))
EXTRACTOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXTRACTOR)


def translate_fixture(body, direction=1, helpers=''):
    source = EXTRACTOR.Source('fixture.cpp', '''
        static void Fixture(PaintSession& session, const Ride& ride,
            uint8_t trackSequence, uint8_t direction, int32_t height,
            const TrackElement& trackElement, SupportType supportType)
        { ''' + body + ' } ' + helpers)
    translator = EXTRACTOR.Translator.__new__(EXTRACTOR.Translator)
    translator.constants = {}
    translator.by_name = {name: source for name in source.functions}
    parts = []
    translator.paint(source, 'Fixture', 0, direction, 40, 0, parts)
    return parts


def rail(image):
    return ('PaintAddImageAsParent(session, session.TrackColours.WithIndex(%d), '
            '{0,0,height}, {32,20,2});' % image)


class NativeTrackExtractorTest(unittest.TestCase):
    def test_shared_cases_default_and_intentional_fallthrough(self):
        body = ('switch (direction) { '
                'case 0: case 1: ' + rail(100) + ' break; '
                'default: ' + rail(300) + ' break; '
                'case 2: ' + rail(200) +
                'case 3: ' + rail(201) + ' break; } ' + rail(400))
        for direction, expected in ((0, [100, 400]), (1, [100, 400]),
                                    (2, [200, 201, 400]), (3, [201, 400]),
                                    (4, [300, 400])):
            with self.subTest(direction=direction):
                self.assertEqual([p[0] for p in translate_fixture(body, direction)], expected)

    def test_nested_switch_break_stops_only_innermost_switch(self):
        body = ('switch (direction) { case 1: '
                'switch (trackSequence) { case 0: { ' + rail(100) +
                ' break; } default: ' + rail(101) + ' break; } ' + rail(200) +
                ' break; default: ' + rail(300) + ' break; } ' + rail(400))
        self.assertEqual([p[0] for p in translate_fixture(body)], [100, 200, 400])

    def test_break_in_auxiliary_block_still_exits_graphical_switch(self):
        body = ('switch (direction) { case 0: case 1: '
                'if (direction == 1) { PaintUtilSetGeneralSupportHeight(session, height); break; } '
                + rail(100) + ' break; default: ' + rail(200) + ' break; } ' + rail(300))
        self.assertEqual([p[0] for p in translate_fixture(body, 1)], [300])
        self.assertEqual([p[0] for p in translate_fixture(body, 0)], [100, 300])

    def test_return_propagates_through_nested_blocks_and_switches(self):
        body = ('switch (direction) { case 1: { ' + rail(100) +
                ' switch (trackSequence) { case 0: if (direction == 1) { return; } '
                + rail(200) + ' break; } ' + rail(300) + ' break; } '
                'default: break; } ' + rail(400))
        self.assertEqual([p[0] for p in translate_fixture(body)], [100])
        self.assertEqual([p[0] for p in translate_fixture(body, 0)], [400])

    def test_helper_return_does_not_return_from_caller(self):
        helper = ('''static void Helper(PaintSession& session, const Ride& ride,
            uint8_t trackSequence, uint8_t direction, int32_t height,
            const TrackElement& trackElement, SupportType supportType) {
            if (direction == 1) { ''' + rail(100) + ' return; } ' + rail(200) + ' }')
        body = ('Helper(session, ride, trackSequence, direction, height, trackElement, supportType); '
                + rail(300))
        self.assertEqual([p[0] for p in translate_fixture(body, 1, helper)], [100, 300])
        self.assertEqual([p[0] for p in translate_fixture(body, 0, helper)], [200, 300])

    def test_real_heartline_prepend_assignment_emits_rail_before_front_rail(self):
        # Exercise the actual authored function, including chain and non-chain
        # branches. The support-pointer assignment must not swallow its RHS.
        path = ROOT / 'src/openrct2/paint/track/coaster/HeartlineTwisterCoaster.cpp'
        source = EXTRACTOR.Source(str(path), path.read_text())
        translator = EXTRACTOR.Translator.__new__(EXTRACTOR.Translator)
        translator.constants = {}
        translator.by_name = {name: source for name in source.functions}
        for state, first in ((0, 21346), (1, 21402)):
            for direction in range(4):
                with self.subTest(state=state, direction=direction):
                    parts = []
                    translator.paint(source, 'HeartlineTwisterRCTrack60DegUp',
                                     0, direction, 40, state, parts)
                    self.assertEqual([p[0] for p in parts],
                                     [first + direction, first + 4 + direction])
                    self.assertEqual(parts[0][1:4], (0, 0, 40))
                    self.assertEqual(parts[0][4:10],
                                     (6, 0, 40, 20, 32, 2) if direction & 1
                                     else (0, 6, 40, 32, 20, 2))
                    self.assertEqual(parts[1][4:10],
                                     (27, 0, 40, 1, 32, 88) if direction & 1
                                     else (0, 27, 40, 32, 1, 88))
                    self.assertEqual([p[10:] for p in parts], [(0, -1), (0, -1)])

    def test_alias_prepend_and_reused_geometry_preserve_source_values(self):
        parts = translate_fixture('''
            auto image = session.TrackColours.WithIndex(21403);
            const auto offset = { 2, 6, height };
            const auto bounds = { { 3, 7, height }, { 32, 20, 2 } };
            session.WoodenSupportsPrependTo = PaintAddImageAsParentRotated(
                session, direction, image, offset, bounds);
            PaintAddImageAsChildRotated(session, direction,
                image.WithIndexOffset(4), offset, bounds);
        ''')
        self.assertEqual(parts, [
            (21403, 6, 2, 40, 7, 3, 40, 20, 32, 2, 0, -1),
            (21407, 6, 2, 40, 7, 3, 40, 20, 32, 2, 0, 0)])

    def test_unowned_draw_valued_declaration_is_rejected(self):
        with self.assertRaisesRegex(EXTRACTOR.Unsupported, 'draw-valued declaration'):
            translate_fixture('''
                auto unexpected = PaintAddImageAsParentRotated(session, direction,
                    session.TrackColours.WithIndex(21403), {0,0,height}, {32,20,2});
            ''')

    def test_unknown_graphical_branch_and_value_return_are_rejected(self):
        statements = [
            'if (trackElement.hasCableLift()) UnknownRail(session);',
            'UnknownRail(session);',
            'return PaintAddImageAsParent(session, image, offset, bounds);']
        for statement in statements:
            with self.subTest(statement=statement), self.assertRaises(EXTRACTOR.Unsupported):
                translate_fixture(statement)

    def test_integer_division_and_remainder_match_cxx(self):
        for expression, expected in (('-7 / 3', -2), ('7 / -3', -2),
                                     ('-7 % 3', -1), ('7 % -3', 1)):
            self.assertEqual(EXTRACTOR.evaluate(EXTRACTOR.tokens(expression), {}), expected)


if __name__ == '__main__':
    unittest.main()
