"""Focused authoring regressions; no game, C++ painter, compiler or GPU execution."""
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    'native_track_extractor', Path(__file__).with_name('extract-native-track-recipes.py'))
EXTRACTOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXTRACTOR)


def translate_fixture(body, direction=1, helpers='', state=0):
    source = EXTRACTOR.Source('fixture.cpp', '''
        static void Fixture(PaintSession& session, const Ride& ride,
            uint8_t trackSequence, uint8_t direction, int32_t height,
            const TrackElement& trackElement, SupportType supportType)
        { ''' + body + ' } ' + helpers)
    translator = EXTRACTOR.Translator.__new__(EXTRACTOR.Translator)
    translator.constants = {}
    translator.by_name = {name: source for name in source.functions}
    parts = []
    translator.paint(source, 'Fixture', 0, direction, 40, state, parts)
    return parts


def rail(image):
    return ('PaintAddImageAsParent(session, session.TrackColours.WithIndex(%d), '
            '{0,0,height}, {32,20,2});' % image)


class NativeTrackExtractorTest(unittest.TestCase):
    def test_station_marker_preserves_parameters_and_platform_branch(self):
        body = ('bool drewStation = TrackPaintUtilDrawStation2(session, ride, direction, height, '
                'trackElement, StationBaseType::b, -2, 5, 7); '
                'if (drewStation) { '+rail(100)+' } else { '+rail(101)+' }')
        for state, image in ((0,101),(64,100)):
            parts=translate_fixture(body,state=state)
            self.assertEqual(parts[0],(EXTRACTOR.STATION_PART,0,0,40,2,-2,5,7,0,0,0,-1))
            self.assertEqual(parts[1][0],image)

    def test_new_graphical_state_bits_select_source_branches(self):
        for condition,bit in (('trackElement.hasCableLift()',8),('IsCsgLoaded()',16),
                              ('trackElement.hasGreenLight()',32)):
            body='if ('+condition+') { '+rail(100)+' } else { '+rail(101)+' }'
            self.assertEqual(translate_fixture(body,state=bit)[0][0],100)
            self.assertEqual(translate_fixture(body,state=0)[0][0],101)

    def test_photo_base_and_wooden_colour_roles(self):
        self.assertEqual(translate_fixture('TrackPaintUtilOnridePhotoPlatformPaintBase(session,height);')[0],
                         (22432,0,0,40,0,0,40,32,32,1,2,-1))
        for expression,role in (('WoodenRCGetTrackColour<false>(session)',1),
                                ('WoodenRCGetRailsColour(session)',0),('GetTrackColour(session)',3)):
            body='PaintAddImageAsParent(session, '+expression+'.WithIndex(100), {0,0,height}, {32,20,2});'
            self.assertEqual(translate_fixture(body)[0][10],role)

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
            'if (trackElement.unknownGraphicalFlag()) UnknownRail(session);',
            'UnknownRail(session);',
            'return PaintAddImageAsParent(session, image, offset, bounds);']
        for statement in statements:
            with self.subTest(statement=statement), self.assertRaises(EXTRACTOR.Unsupported):
                translate_fixture(statement)

    def test_integer_division_and_remainder_match_cxx(self):
        for expression, expected in (('-7 / 3', -2), ('7 / -3', -2),
                                     ('-7 % 3', -1), ('7 % -3', 1)):
            self.assertEqual(EXTRACTOR.evaluate(EXTRACTOR.tokens(expression), {}), expected)

    def test_array_pointer_offset_uses_suffix_and_rejects_out_of_bounds(self):
        self.assertEqual(EXTRACTOR.evaluate(EXTRACTOR.tokens('images + 4'),{'images':list(range(8))}),[4,5,6,7])
        with self.assertRaises(EXTRACTOR.Unsupported):
            EXTRACTOR.evaluate(EXTRACTOR.tokens('images + 9'),{'images':list(range(8))})

    def test_cpp_array_indices_never_use_python_negative_wraparound(self):
        for index in (-4,-1,4):
            with self.subTest(index=index),self.assertRaisesRegex(EXTRACTOR.Unsupported,'array index'):
                EXTRACTOR.evaluate(EXTRACTOR.tokens('images[index]'),{'images':[1,2,3,4],'index':index})

    def test_support_local_block_cannot_hide_graphics_or_outer_mutation(self):
        self.assertEqual([p[0] for p in translate_fixture('''
            if (TrackPaintUtilShouldPaintSupports(session.MapPosition)) {
                uint16_t ax = direction == 0 ? 5 : 3;
                MetalASupportsPaintSetup(session, supportType, ax);
            }
        '''+rail(100))],[100])
        for body in (rail(100),'direction = 2;'):
            with self.assertRaises(EXTRACTOR.Unsupported):
                translate_fixture('if (TrackPaintUtilShouldPaintSupports(session.MapPosition)) {'+body+'}')


class NativeTrackExpandedSourceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.translator=EXTRACTOR.Translator(ROOT)

    def parts(self,style,track_type,sequence=0,direction=0,state=0):
        t=self.translator;source,name=t.getter(t.getters[style],track_type,state=state)
        result=[];t.paint(source,name,sequence,direction,0,state,result,track_type=track_type)
        return result

    def test_junior_water_chain_and_station_brakes_remain_distinct(self):
        for direction in range(4):
            for style,chain in ((31,27913),(78,27983)):
                self.assertEqual(self.parts(style,0,direction=direction)[0][0],27807+(direction&1))
                self.assertEqual(self.parts(style,0,direction=direction,state=1)[0][0],chain+(direction&1))
            self.assertNotEqual(self.parts(31,1,direction=direction,state=4)[0][0],
                                self.parts(31,1,direction=direction,state=0)[0][0])
            self.assertEqual(self.parts(78,1,direction=direction,state=4),self.parts(78,1,direction=direction,state=0))

    def test_junior_water_s_bend_four_tiles_and_reversed_directions(self):
        for style in (31,78):
            for track_type,base in ((38,27901),(39,27897)):
                for direction in range(4):
                    for sequence in range(4):
                        mapped=3-sequence if direction>=2 else sequence
                        expected=base+mapped if not direction&1 else (27908 if track_type==38 else 27912)-mapped
                        parts=self.parts(style,track_type,sequence,direction)
                        self.assertEqual([p[0] for p in parts],[expected])
                    for sequence in range(4,8):
                        with self.assertRaisesRegex(EXTRACTOR.Unsupported,'array index'):
                            self.parts(style,track_type,sequence,direction)

    def test_junior_water_transitions_keep_subtype_chain_images(self):
        for style in (31,78):
            for track_type,base in ((6,27811),(9,27819)):
                self.assertEqual(self.parts(style,track_type)[0][0],base)
                for direction in range(4):
                    self.assertEqual(len(self.parts(style,track_type,direction=direction)),1)
                    self.assertNotEqual(self.parts(style,track_type,direction=direction,state=1)[0][0],
                                        self.parts(style,track_type,direction=direction)[0][0])

    def test_original_quarter_helix_first_and_reversed_component_geometry(self):
        self.assertEqual(self.parts(39,102),[
            (35301,0,0,0,0,6,1,32,20,1,0,-1),(35302,0,0,0,0,25,3,32,1,27,0,-1)])
        self.assertEqual(self.parts(39,104),[(35330,0,0,0,0,25,11,32,1,27,0,-1)])
        # Independent TED reverse sequence and rotation, all seven source tiles.
        for down,up,rotation in ((104,103,1),(105,102,3),(108,107,1),(109,106,3)):
            for sequence,reversed_sequence in enumerate((6,4,5,3,1,2,0)):
                for direction in range(4):
                    self.assertEqual(self.parts(39,down,sequence,direction),
                                     self.parts(39,up,reversed_sequence,(direction+rotation)&3))

    def test_narrow_pier_and_no_platform_graphical_branch(self):
        for style,variant,offset in ((27,4,10),(1,4,5),(56,4,5),(3,5,0),(69,5,0)):
            markers=[p for p in self.parts(style,1) if p[0]==EXTRACTOR.STATION_PART]
            self.assertEqual(len(markers),1)
            self.assertEqual(markers[0][8],variant)
            if style in (27,3,69): self.assertEqual(markers[0][6],offset)
        # Splash Boats draws a front rail only when no platform hides it.
        self.assertEqual(len(self.parts(65,1,state=0)),len(self.parts(65,1,state=64))+1)

    def test_junior_legacy_curve_helpers_and_zero_initialised_second_sprite(self):
        self.assertEqual([p[0] for p in self.parts(31,16)],[27842])
        # Right half helix source initializes only image0 on this tile; image1
        # must stay absent, while left/other directions can emit both parents.
        self.assertEqual(len(self.parts(31,88)),1)
        self.assertEqual(len(self.parts(31,87)),2)
        for track_type in (87,88,89,90):
            for sequence in range(8):
                for direction in range(4): self.parts(31,track_type,sequence,direction)


if __name__ == '__main__':
    unittest.main()
