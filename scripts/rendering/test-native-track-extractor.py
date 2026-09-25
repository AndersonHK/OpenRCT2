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
    def test_support_only_switch_keeps_outer_break_and_return_semantics(self):
        body = ('if (TrackPaintUtilShouldPaintSupports(session.MapPosition)) { switch(direction) {'
                'case 0: MetalASupportsPaintSetup(session); break; '
                'default: MetalASupportsPaintSetup(session); break; } } '+rail(901))
        for direction in range(4): self.assertEqual(translate_fixture(body,direction)[0][0],901)
        for control in ('return','break'):
            dangerous=('if (TrackPaintUtilShouldPaintSupports(session.MapPosition)) { '
                       +control+'; } '+rail(902))
            with self.assertRaises(EXTRACTOR.Unsupported): translate_fixture(dangerous)
        with self.assertRaises(EXTRACTOR.Unsupported):
            translate_fixture('int32_t value=0; if (TrackPaintUtilShouldPaintSupports(session.MapPosition)) {'
                              'switch(direction) { case 0: value=7; break; } } '+rail(903))

    def test_front_track_field_provenance_keeps_anchor_pair_and_original_art(self):
        helpers='static constexpr auto sprites={{100,101,102,103}};'
        for classic in (False,True):
            for direction in range(4):
                prefix='TrackPaint<'+str(classic).lower()+'> (session,direction,'
                back=translate_fixture(prefix+'sprites[0].track,sprites[0].handrail,{0,0,height},{{0,27,height+4},{32,1,9}});',
                    direction,helpers)
                front=translate_fixture(prefix+'sprites[0].frontTrack,sprites[0].frontHandrail,{0,0,height},{{0,27,height+4},{32,1,9}});',
                    direction,helpers)
                self.assertEqual([p[0] for p in front],[102] if classic else [102,103])
                self.assertEqual([p[10] for p in front],[8] if classic else [9,8])
                self.assertEqual([p[10] for p in back],[0] if classic else [1,0])
                self.assertEqual([p[1:10]+p[11:] for p in front],[p[1:10]+p[11:] for p in back])
                if not classic: self.assertEqual(front[0][4:10],front[1][4:10])
        # A suggestive constant name alone does not authorize a new depth anchor.
        self.assertEqual(translate_fixture('PaintAddImageAsParent(session,session.TrackColours.WithIndex(SPR_FAKE_FRONT_RAIL),'
            '{0,0,height},{32,1,3});',helpers='constexpr uint32_t SPR_FAKE_FRONT_RAIL=102;')[0][10],0)

    def test_station_cover_requires_matching_owned_edge_and_never_folds_fence_branch(self):
        prefix=('const auto* stationObj=ride.getStationObject(); '
                'auto colour=GetStationColourScheme(session,trackElement); '
                'bool fence=TrackPaintUtilHasFence(EDGE_NE,session.MapPosition,trackElement,ride,session.CurrentRotation); ')
        cover='TrackPaintUtilDrawStationCovers(session,EDGE_NE,fence,stationObj,height,colour);'
        self.assertEqual(translate_fixture(prefix+cover),[(EXTRACTOR.STATION_PART,0,0,40,0,0,0,0,7,0,0,-1)])
        for bad in (cover.replace('EDGE_NE','EDGE_NW'),cover.replace('fence,','true,'),
                    'if(fence) {'+rail(901)+'}',cover.replace('stationObj','otherObject')):
            with self.assertRaises(EXTRACTOR.Unsupported): translate_fixture(prefix+bad)

    def test_immutable_array_pointer_uses_pointee_not_declarator_extent(self):
        env={'table':[[11,12],[21,22]]}
        self.assertTrue(EXTRACTOR.declaration(EXTRACTOR.tokens('const uint32_t (*sprites)[2] = table'),env))
        self.assertEqual(EXTRACTOR.evaluate(EXTRACTOR.tokens('sprites[1][0]'),env),21)
        with self.assertRaises(EXTRACTOR.Unsupported):
            EXTRACTOR.evaluate(EXTRACTOR.tokens('sprites[2][0]'),env)

    def test_spritebb_brace_elision_retains_four_fields_and_rejects_missing_geometry(self):
        fields='{7,{1,2,3},{4,5,6},{7,8,9},8,{2,3,4},{5,6,7},{8,9,10}}'
        v=EXTRACTOR.SpriteBbCArrayDeferred(EXTRACTOR.tokens('{'+fields+'}'),['1','2']).resolve({})
        self.assertEqual(v,[[[7,[1,2,3],[4,5,6],[7,8,9]],[8,[2,3,4],[5,6,7],[8,9,10]]]])
        with self.assertRaises(EXTRACTOR.Unsupported):
            EXTRACTOR.SpriteBbCArrayDeferred(EXTRACTOR.tokens('{{7,{1,2,3}}}'),['1','1']).resolve({})

    def test_partial_box_array_keeps_zero_siblings_before_mirroring(self):
        value=EXTRACTOR.ArrayDeferred(EXTRACTOR.tokens('{{{{{1,2,3},{4,5,6}}}}}'),['3'],box=True).resolve({})
        self.assertEqual(value,[[[1,2,3],[4,5,6]],[[0,0,0],[0,0,0]],[[0,0,0],[0,0,0]]])

    def test_animation_expressions_do_not_admit_time_dependent_branch_or_geometry(self):
        expression='(getGameState().currentTicks / 2) & 7'
        with self.assertRaises(EXTRACTOR.Unsupported):
            translate_fixture('if ('+expression+') {'+rail(901)+'}')
        with self.assertRaises(EXTRACTOR.Unsupported):
            translate_fixture('auto frame='+expression+'; '+
                'PaintAddImageAsParent(session, session.TrackColours.WithIndex(901), {0,0,height}, {32,20,frame});')
        with self.assertRaises(EXTRACTOR.Unsupported):
            EXTRACTOR.evaluate(EXTRACTOR.tokens('(getGameState().currentTicks / 3) % 7'),{})

    def test_clock_indexed_images_require_exact_contiguous_table_and_preserve_colour_role(self):
        frame=EXTRACTOR.evaluate(EXTRACTOR.tokens('(getGameState().currentTicks >> 2) & 3'),{})
        value=EXTRACTOR.evaluate(EXTRACTOR.tokens('frames[frame]'),{'frames':[40,41,42,43],'frame':frame})
        self.assertEqual(value.encode(),0x80000000 | (2<<19) | (2<<22) | 40)
        for table in ([40,42,43,44],[40,41,42],[40,41,42,43,44]):
            with self.assertRaises(EXTRACTOR.Unsupported):
                EXTRACTOR.evaluate(EXTRACTOR.tokens('frames[frame]'),{'frames':table,'frame':frame})
        body='auto support=session.SupportColours; auto track=session.TrackColours; '
        body+='if(track.HasSecondary()) { support=support.WithSecondary(track.GetSecondary()); } '
        body+='PaintAddImageAsParent(session,support.WithIndex(40),{0,0,height},{32,20,2});'
        self.assertEqual(translate_fixture(body)[0][10],1)

    def test_local_track_type_alias_is_value_not_recursive_deferred_reference(self):
        self.assertEqual(translate_fixture('auto trackType=trackElement.getTrackType(); '
                         'if(trackType==0) {'+rail(904)+'}')[0][0],904)

    def test_explicit_box_constructor_preserves_zero_and_signed_geometry(self):
        self.assertEqual(EXTRACTOR.evaluate(EXTRACTOR.tokens('BoundBoxXYZ({0,2,-8},{32,32,2})'),{}),
                         [[0,2,-8],[32,32,2]])
        self.assertEqual(EXTRACTOR.evaluate(EXTRACTOR.tokens('BoundBoxXYZ({}, {})'),{}),[[0,0,0],[0,0,0]])

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

    def test_photo_markers_preserve_source_order_height_small_art_and_tunnel(self):
        for direction in range(4):
            for helper,small in (('TrackPaintUtilOnridePhotoPaint',0),('TrackPaintUtilOnridePhotoSmallPaint',1)):
                parts=translate_fixture(rail(100)+helper+'(session,direction,height+5,trackElement);'+rail(101),direction)
                self.assertEqual(parts[0][0],100)
                self.assertEqual(parts[1],(EXTRACTOR.PHOTO_PART,direction,small,45,0,0,0,0,0,0,2,-1))
                self.assertEqual(parts[2][0],101)
            try:
                EXTRACTOR.CAPTURE_TUNNELS=True
                for tail,expected in (('',43),(',48',43),(',48,2',42)):
                    parts=translate_fixture('TrackPaintUtilOnridePhotoPaint2(session,direction,trackElement,height'+tail+');',direction)
                    self.assertEqual(parts,[(EXTRACTOR.PHOTO_PART,direction,0,expected,0,0,0,0,0,0,2,-1),
                        (EXTRACTOR.TUNNEL_PART,direction&1,6,40,0,0,0,0,0,0,0,-1)])
            finally:
                EXTRACTOR.CAPTURE_TUNNELS=False

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

    def test_go_karts_station_keeps_interleaved_covers_grid_and_signal_state(self):
        # Literal source tables: middle/begin grid20764..67/front20768..71;
        # end grid20756..59/front20760..63. Covers stay between calls.
        red=((20808,20814),(20810,20816),(20811,20817),(20812,20818))
        green=((20809,20815),(20810,20816),(20811,20817),(20813,20819))
        for kind in (1,2,3):
            for direction in range(4):
                for light in (0,32):
                    parts=self.parts(24,kind,direction=direction,state=light)
                    self.assertEqual([p[0] for p in parts[:4]],[
                        (20756 if kind==1 else 20764)+direction,EXTRACTOR.STATION_PART,
                        (20760 if kind==1 else 20768)+direction,EXTRACTOR.STATION_PART])
                    self.assertEqual([parts[i][1] for i in (1,3)],( [0,2] if direction&1 else [3,1]))
                    self.assertTrue(all(parts[i][8]==7 and parts[i][2]==0 for i in (1,3)))
                    self.assertEqual(parts[0][4:10],(2,0,0,28,32,1) if direction&1 else (0,2,0,32,28,1))
                    self.assertEqual(parts[2][4:10],(29,0,2,1,32,3) if direction&1 else (0,29,2,32,1,3))
                    self.assertEqual(tuple(p[0] for p in parts[4:]),(green if light else red)[direction] if kind==1 else ())
                    self.assertEqual(parts,self.parts(24,kind,direction=direction,state=light|64))
                    self.assertTrue(all(p[-1]==-1 for p in parts))

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


    def test_inverted_slope_support_branch_does_not_discard_authored_rails(self):
        for direction in range(4):
            self.assertEqual(self.parts(11,4,direction=direction)[0],
                (26569+direction,0,0,29,6 if direction&1 else 0,0 if direction&1 else 6,45,
                 20 if direction&1 else 32,32 if direction&1 else 20,3,0,-1))
            self.assertEqual(self.parts(11,4,direction=direction,state=1)[0][0],26621+direction)
        for style in (11,28,30,35,54,71):
            for kind in (4,6,9,10,12,15):
                for direction in range(4): self.assertTrue(self.parts(style,kind,direction=direction))

    def test_wooden_array_templates_keep_body_rail_child_and_classic_choice(self):
        modern=self.parts(79,18)
        self.assertEqual(modern[0][0],23497)
        self.assertEqual(modern[0][-2:],(1,-1))
        self.assertEqual(modern[1][-2:],(0,0))
        classic=self.parts(9,18)
        self.assertEqual(len(classic),1)
        self.assertEqual(classic[0][0],23497)
        self.assertEqual(classic[0][-2:],(0,-1))
        for kind in (16,17,22,44,87,91,137,158,178):
            for sequence in range({16:7,17:7,22:7,44:4,87:8,91:14,137:5,158:4,178:4}[kind]):
                for direction in range(4): self.parts(79,kind,sequence,direction)

    def test_restored_station_platform_and_brake_variants(self):
        for direction in range(4):
            axis=direction&1
            for kind in (1,2,3):
                for platforms in (False,True):
                    for closed in (False,True):
                        state=(64 if platforms else 0)|(4 if closed else 0)
                        parts=self.parts(53,kind,direction=direction,state=state)
                        expected=(15810 if platforms else 16218)+axis
                        if kind==1: expected+=2+(2 if closed else 0)
                        self.assertEqual(parts[0][0],expected)
                        self.assertEqual(len(parts),2 if platforms else 1)
                        if platforms: self.assertEqual(parts[1][8],6)
                        mouse=self.parts(67,kind,direction=direction,state=state)
                        self.assertTrue(any(p[0]==EXTRACTOR.STATION_PART for p in mouse))

    def test_junior_eighth_and_sloped_curves_and_mouse_struct_geometry(self):
        self.assertEqual(self.parts(31,133)[0],(28301,0,0,0,0,6,0,32,20,1,0,-1))
        self.assertEqual(self.parts(31,46)[0],(28096,0,6,0,0,6,0,32,20,1,0,-1))
        self.assertEqual(self.parts(67,42)[0],(17003,0,0,0,0,6,0,32,20,3,0,-1))
        self.assertEqual(self.parts(67,46)[0],(17021,0,6,0,0,6,0,32,20,3,0,-1))
        for style,kind,sequence in ((31,133,5),(67,42,4)):
            with self.assertRaises(EXTRACTOR.Unsupported): self.parts(style,kind,sequence)
        for style in (31,78):
            for kind in (46,47,48,49,133,134,135,136,137,138,139,140):
                for sequence in range(4 if kind<100 else 5):
                    for direction in range(4): self.parts(style,kind,sequence,direction)
        for kind in (42,43,46,47,48,49):
            for sequence in range(4):
                for direction in range(4): self.parts(67,kind,sequence,direction)

    def test_classic_wooden_empty_bounding_entries_and_diagonal_helpers(self):
        for kind in (22,23,44,45):
            for sequence in range(7 if kind<40 else 4):
                for direction in range(4): self.parts(9,kind,sequence,direction)
        self.assertEqual(self.parts(9,23,sequence=1),[])
        for style,thickness in ((11,3),(30,1)):
            for kind in (141,337,338):
                for sequence,direction in enumerate((3,0,2,1)):
                    part=self.parts(style,kind,sequence,direction)[0]
                    self.assertEqual(part[1:10],(-16,-16,29,-16,-16,29,32,32,thickness))
            self.assertNotEqual(self.parts(style,141,0,3),self.parts(style,141,0,3,state=1))
            if style==11:
                self.assertNotEqual(self.parts(style,338,0,3),self.parts(style,338,0,3,state=4))
            else:
                # InvertedRC's diagonal block-brake getter deliberately reuses
                # its static brake art; closed state changes no source sprite.
                self.assertEqual(self.parts(style,338,0,3),self.parts(style,338,0,3,state=4))

    def test_classic_standup_diagonal_templates_and_log_flume_bound_heights(self):
        self.assertEqual(self.parts(8,158,0,3)[0][0],25726)
        for kind in range(158,172):
            for sequence in range(4):
                for direction in range(4): self.parts(8,kind,sequence,direction)
        for kind in (42,43):
            for sequence in (0,2,3):
                for direction in range(4):
                    parts=self.parts(38,kind,sequence,direction)
                    self.assertEqual(len(parts),2)
                    self.assertEqual([p[6] for p in parts],[0,27])
                    self.assertEqual([p[9] for p in parts],[2,0])

    def test_wooden_water_splash_retains_symbolic_filters_in_child_order(self):
        for style in (9,10,79):
            for sequence in range(3):
                for direction in range(4):
                    parts=self.parts(style,117,sequence,direction)
                    water=[(i,p) for i,p in enumerate(parts) if p[10]>=4]
                    if water:
                        self.assertEqual([p[10] for _,p in water],[4,5])
                        self.assertEqual(water[1][0],water[0][0]+1)
                        for _,part in water:
                            self.assertEqual(part[0],0)
                            self.assertGreaterEqual(part[-1],0)
                            self.assertEqual(parts[part[-1]][-1],-1)
        self.assertEqual([p[10] for p in self.parts(79,117)], [1,0,4,5,1,1,1,0])

    def test_inverted_multidimension_function_table_selects_distinct_rails(self):
        self.assertEqual(self.parts(54,0)[0][0],26227)
        self.assertEqual(self.parts(54,16)[0][0],26310)
        self.assertNotEqual(self.parts(54,0),self.parts(53,0))

    def test_source_river_rapids_fixed_channel_and_all_slope_views(self):
        for direction in range(4):
            self.assertEqual([p[0] for p in self.parts(58,0,direction=direction)],
                             [21132+direction,21136+direction])
            for kind,base in ((4,21156),(6,21140),(9,21148),(10,21180),(12,21172),(15,21164)):
                parts=self.parts(58,kind,direction=direction)
                view=(direction+2)%4 if kind in (10,12,15) else direction
                self.assertEqual([p[0] for p in parts],[base+view,base+view+4])
            for kind in (1,2,3):
                parts=self.parts(58,kind,direction=direction,state=64)
                self.assertEqual([p[0] for p in parts[:2]],[21132+direction,21136+direction])
                self.assertEqual(parts[2][0],EXTRACTOR.STATION_PART)
        # Animated mechanisms are checked separately against complete source periods.

    def test_river_rapids_animation_tags_match_every_source_frame_and_preserve_geometry(self):
        for kind,shift,bits,count in ((112,1,3,5),(113,1,3,2),(120,2,4,3)):
            for direction in range(4):
                symbolic=self.parts(58,kind,direction=direction)
                self.assertEqual(len(symbolic),count)
                animated=[i for i,p in enumerate(symbolic) if p[0]&0x80000000]
                self.assertEqual(animated,{112:[1,2,4],113:[0],120:[1]}[kind])
                for i in animated:
                    self.assertEqual((symbolic[i][0]>>19)&7,shift)
                    self.assertEqual((symbolic[i][0]>>22)&7,bits)
                try:
                    for tick in list(range(1<<(shift+bits)))+[0xfffffffe,0xffffffff]:
                        EXTRACTOR.AUTHORING_TICK_OVERRIDE=tick
                        literal=self.parts(58,kind,direction=direction)
                        self.assertEqual(len(literal),len(symbolic))
                        for authored,original in zip(symbolic,literal):
                            word=authored[0]
                            expected=(word&0x7ffff)+((tick>>shift)&((1<<bits)-1)) if word&0x80000000 else word
                            self.assertEqual(expected,original[0])
                            self.assertEqual(authored[1:],original[1:])
                finally: EXTRACTOR.AUTHORING_TICK_OVERRIDE=None
        # Explicit source IDs guard against both paths accidentally sharing a bad base.
        self.assertEqual([p[0]&0x7ffff for p in self.parts(58,112)],[21204,21212,21244,21208,21228])
        self.assertEqual([p[0]&0x7ffff for p in self.parts(58,113,direction=1)],[21269,21277])
        self.assertEqual(self.parts(58,120)[1][0]&0x7ffff,21278)

    def test_source_five_tile_curves_and_gokart_eighths_cover_every_sequence(self):
        for style in (1,14,15,65):
            for kind in (16,17):
                for direction in range(4):
                    for sequence in range(7):
                        parts=self.parts(style,kind,sequence,direction)
                        self.assertEqual(bool(parts),sequence not in (1,4))
                        if style==65 and parts: self.assertEqual(len(parts),2)
        for kind in (134,135):
            for direction in range(4):
                for sequence in range(5):
                    parts=self.parts(24,kind,sequence,direction)
                    self.assertTrue(parts)
                    self.assertTrue(all(p[0]!=0xffffffff for p in parts))

    def test_gokarts_single_initialized_box_is_not_split_into_coordinates(self):
        # GoKarts.cpp kGoKartsLeftQuarterTurn5TilesBoundBoxes[0][1] has
        # one initialized box and two zero siblings inside std::array<...,3>.
        part=self.parts(24,16,sequence=1,direction=0)[0]
        self.assertEqual(part,(35725,0,0,0,0,16,0,16,16,1,0,-1))
        part=self.parts(24,16,sequence=4,direction=2)[0]
        self.assertEqual(part[4:10],(16,0,0,16,16,1))

    def test_monorail_eighth_curve_tables_keep_authored_xy_bounds(self):
        for kind in (133,134,135,136):
            for direction in range(4):
                rows=[self.parts(50,kind,sequence,direction) for sequence in range(5)]
                self.assertEqual(sum(bool(row) for row in rows),4)
                self.assertTrue(all(len(row)<=1 for row in rows))
        self.assertEqual(self.parts(50,134)[0][4:10],(0,6,0,32,20,2))
        self.assertEqual(self.parts(50,134,sequence=4)[0][4:10],(16,0,0,16,16,2))

    def test_source_spinning_tunnels_keep_clock_frames_back_child_front_parent_and_colours(self):
        for style in (5,23,46):
            for direction in range(4):
                symbolic=self.parts(style,173,direction=direction)
                self.assertEqual([p[-1] for p in symbolic[-2:]],[0,-1])
                self.assertEqual([p[10] for p in symbolic[-2:]],[1,1])
                self.assertEqual(symbolic[-2][9],3 if style==23 else 1)
                try:
                    for tick in list(range(16))+[0xffffffff]:
                        EXTRACTOR.AUTHORING_TICK_OVERRIDE=tick
                        literal=self.parts(style,173,direction=direction)
                        self.assertEqual([p[0] for p in literal[-2:]],
                            [28865+(direction&1)*4+(tick//4)%4,28873+(direction&1)*4+(tick//4)%4])
                        self.assertEqual(len(symbolic),len(literal))
                        for authored,original in zip(symbolic,literal):
                            word=authored[0]
                            expected=(word&0x7ffff)+((tick>>2)&3) if word&0x80000000 else word
                            self.assertEqual(expected,original[0])
                            self.assertEqual(authored[1:],original[1:])
                finally: EXTRACTOR.AUTHORING_TICK_OVERRIDE=None

    def test_gokarts_height_relative_nested_array_art(self):
        for kind,images in ((5,[35621,35622]),(7,[35605,35606]),(8,[35613,35614]),(16,[35723,35724])):
            self.assertEqual([p[0] for p in self.parts(24,kind)],images)
        # Undefined image entries are omitted just as GfxGetG1Element rejects them.
        for direction in (1,2):
            parts=self.parts(24,5,direction=direction)
            self.assertEqual(len(parts),1)
            self.assertEqual(parts[0][3],0)


class TunnelAuthoringTest(unittest.TestCase):
    def test_rotated_and_vertical_markers_keep_signed_world_height(self):
        EXTRACTOR.CAPTURE_TUNNELS=True
        try:
            for direction in range(4):
                result=translate_fixture('PaintUtilPushTunnelRotated(session,direction,height-8,2); '
                    'PaintUtilSetVerticalTunnel(session,height+32);',direction)
                self.assertEqual(result,[(EXTRACTOR.TUNNEL_PART,direction&1,2,32,0,0,0,0,0,0,0,-1),
                                         (EXTRACTOR.TUNNEL_PART,2,0,72,0,0,0,0,0,0,0,-1)])
        finally: EXTRACTOR.CAPTURE_TUNNELS=False

    def test_station_tunnel_helpers_are_metadata_not_discarded_auxiliary(self):
        EXTRACTOR.CAPTURE_TUNNELS=True
        try:
            for name,kind in (('TrackPaintUtilDrawStationTunnel',6),('TrackPaintUtilDrawStationTunnelTall',9)):
                for direction in range(4):
                    result=translate_fixture(rail(100)+name+'(session,direction,height);',direction)
                    self.assertEqual(result[-1],(EXTRACTOR.TUNNEL_PART,direction&1,kind,40,0,0,0,0,0,0,0,-1))
                    self.assertEqual(result[0][0],100)
            translator=EXTRACTOR.Translator(ROOT)
            for style,kind in ((39,6),(30,9)):
                for direction in range(4):
                    source,name=translator.getter(translator.getters[style],1,state=0)
                    parts=[];translator.paint(source,name,0,direction,0,0,parts,track_type=1)
                    requests=[p for p in parts if p[0]==EXTRACTOR.TUNNEL_PART]
                    self.assertEqual(requests,[(EXTRACTOR.TUNNEL_PART,direction&1,kind,0,0,0,0,0,0,0,0,-1)])
        finally: EXTRACTOR.CAPTURE_TUNNELS=False

    def test_tunnel_branch_is_not_discarded_with_auxiliary_supports(self):
        EXTRACTOR.CAPTURE_TUNNELS=True
        try:
            for direction in range(4):
                result=translate_fixture('if(direction==0 || direction==3) { '
                    'PaintUtilPushTunnelRotated(session,direction,height,0); }',direction)
                self.assertEqual(len(result),int(direction in (0,3)))
        finally: EXTRACTOR.CAPTURE_TUNNELS=False

    def test_tunnel_does_not_steal_sprite_child_parent(self):
        EXTRACTOR.CAPTURE_TUNNELS=True
        try:
            result=translate_fixture(rail(100)+'PaintUtilPushTunnelLeft(session,height,0); '
                'PaintAddImageAsChild(session,session.TrackColours.WithIndex(101),{0,0,height},{32,20,2});')
            self.assertEqual(result[-1][-1],0)
        finally: EXTRACTOR.CAPTURE_TUNNELS=False

    def test_ghost_train_doors_remain_dynamic_raw_selectors(self):
        expected={'kDoorOpeningInwardsToImage[trackElement.getDoorAState()]':258,
                  'kDoorOpeningOutwardsToImage[trackElement.getDoorBState()]':257,
                  'kDoorFlatTo25DegOpeningInwardsToImage[trackElement.getDoorAState()]':262}
        for expression,selector in expected.items():
            self.assertEqual(EXTRACTOR.evaluate(EXTRACTOR.tokens(expression),{}),selector)
        for direction in range(4):
            self.assertEqual(EXTRACTOR.evaluate(EXTRACTOR.tokens('GetTunnelDoorsImageStraightFlat(trackElement,direction)'),
                {'direction':direction}),258 if direction in (0,3) else 257)

    def test_height_helper_offsets_both_image_and_bounds(self):
        result=translate_fixture('PaintAddImageAsParentHeight(session,session.TrackColours.WithIndex(123),'
            'height,{2,3,4},{{5,6,7},{8,9,10}});')
        self.assertEqual(result,[(123,2,3,44,5,6,47,8,9,10,0,-1)])


class MetalSupportAuthoringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.translator=EXTRACTOR.SupportTranslator(ROOT)

    def capture(self,body,direction=0):
        source=EXTRACTOR.Source('supports.cpp','void Fixture() {'+body+'}')
        t=self.translator;t.support_ops=[];t.support_gaps=set();t.support_predicate=0;parts=[]
        t.paint(source,'Fixture',0,direction,40,0,parts)
        return parts,list(t.support_ops)

    def test_checker_branches_are_conditional_and_keep_source_order(self):
        body=(rail(100)+'if(TrackPaintUtilShouldPaintSupports(session.MapPosition)) {'
            'MetalASupportsPaintSetup(session,supportType.metal,MetalSupportPlace::centre,0,height,session.SupportColours);'
            '} else {MetalBSupportsPaintSetupRotated(session,MetalSupportType::fork,MetalSupportPlace::topCorner,direction,-8,height+16,session.SupportColours);}'
            'PaintUtilSetSegmentSupportHeight(session,PaintUtilRotateSegments(EnumsToFlags(PaintSegment::top,PaintSegment::centre),direction),0xffff,0);'
            'PaintUtilSetGeneralSupportHeight(session,height+32);'+rail(101))
        parts,ops=self.capture(body,1)
        self.assertEqual([p[0] for p in parts],[100,101])
        self.assertEqual(ops,[(1,255,4,4,40,0,0,0,1,1,1,0),(2,1,0,1,56,-8,0,0,2,1,1,0),
                             (3,0,0,4,65535,0,260,0,0,1,1,0),(4,0,0,4,72,0,0,32,0,1,1,0)])

    def test_side_by_side_rotates_graphic_without_rotating_placement_twice(self):
        for direction in range(4):
            _,ops=self.capture('DrawSupportsSideBySide(session,direction,height,session.SupportColours,supportType.metal,4);',direction)
            self.assertEqual([op[2] for op in ops],[6,7] if direction&1 else [5,8])
            self.assertTrue(all(op[3]==direction and op[11]==1 for op in ops))

    def test_all_eight_explicit_metal_types_and_negative_extra_remain_raw(self):
        for expected,name in enumerate(('tubes','fork','boxed','stick','thick','truss','tubesInverted','boxedCoated')):
            _,ops=self.capture('MetalBSupportsPaintSetup(session,MetalSupportType::'+name+
                              ',MetalSupportPlace::bottomRightSide,-16,height-8,session.SupportColours);')
            self.assertEqual(ops,[(2,expected,8,4,32,-16,0,0,0,1,0,0)])

    def test_nested_checker_never_runs_contradictory_branch(self):
        metal='MetalASupportsPaintSetup(session,supportType.metal,MetalSupportPlace::centre,0,height,session.SupportColours);'
        _,ops=self.capture('if(TrackPaintUtilShouldPaintSupports(session.MapPosition)) {'
                           'if(!TrackPaintUtilShouldPaintSupports(session.MapPosition)) {'+metal+'}}')
        self.assertFalse(ops)

    def test_unsupported_wooden_call_is_explicit_without_dropping_rail(self):
        parts,ops=self.capture('WoodenASupportsPaintSetup(session);'+rail(77))
        self.assertEqual(parts[0][0],77);self.assertFalse(ops)
        self.assertIn('WoodenASupportsPaintSetup',self.translator.support_gaps)

    def test_checker_cannot_silently_capture_a_rail_branch(self):
        with self.assertRaises(EXTRACTOR.Unsupported):
            self.capture('if(TrackPaintUtilShouldPaintSupports(session.MapPosition)) {'+rail(5)+'}')

    def test_real_looping_flat_keeps_rails_and_all_three_support_operations(self):
        t=self.translator
        for direction in range(4):
            source,name=t.getter(t.getters[39],0)
            t.support_ops=[];t.support_gaps=set();t.support_predicate=0;parts=[]
            t.paint(source,name,0,direction,0,0,parts,track_type=0)
            self.assertEqual(parts[0][0],15004+(direction&1))
            self.assertEqual([op[0] for op in t.support_ops],[1,3,4])
            self.assertEqual(t.support_ops[0][8],1)
            self.assertFalse(t.support_gaps)


class WoodenAuthoringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.t=EXTRACTOR.WoodenSupportTranslator(ROOT)
    def capture(self,body,direction=0,sequence=0):
        source=EXTRACTOR.Source('fixture.cpp','void Fixture(){'+body+'}')
        t=self.t;t.support_ops=[];t.support_gaps=set();t.support_predicate=0;parts=[]
        t.paint(source,'Fixture',sequence,direction,40,0,parts)
        return parts,t.support_ops
    def test_unrotated_direction_is_not_discarded(self):
        _,ops=self.capture('WoodenASupportsPaintSetup(session,WoodenSupportType::mine,WoodenSupportSubType::corner2,height-8,session.SupportColours,WoodenSupportTransitionType::up25Deg,3);')
        self.assertEqual(ops[0],(5,1,4,3,32,2,0,0,0,1,0,0))
    def test_rotated_default_type_and_transition(self):
        for d in range(4):
            _,ops=self.capture('WoodenBSupportsPaintSetupRotated(session,supportType.wooden,WoodenSupportSubType::nwSe,direction,height,session.SupportColours);',d)
            self.assertEqual(ops[0],(6,255,1,d,40,255,0,0,0,1,0,2))
    def test_none_subtype_does_not_create_art(self):
        _,ops=self.capture('WoodenASupportsPaintSetup(session,WoodenSupportType::truss,WoodenSupportSubType::null,height,session.SupportColours);')
        self.assertEqual(ops,[])
    def test_source_ted_flat_and_up25(self):
        self.assertEqual(self.t.wooden_sequence(0,0),(0,255,0))
        self.assertEqual(self.t.wooden_sequence(4,0),(0,2,0))
        self.assertEqual(self.t.wooden_sequence(0,15),(6,255,0))
        _,ops=self.capture('DrawSupportForSequenceA<TrackElemType::up25>(session,supportType.wooden,trackSequence,direction,height+16,session.SupportColours);',2)
        self.assertEqual(ops[0],(5,255,0,2,56,2,0,0,0,1,0,2))
    def test_non_template_ted_helper(self):
        _,ops=self.capture('DrawSupportForSequenceB(session,WoodenSupportType::mine,TrackElemType::flat,trackSequence,direction,height,session.TrackColours);',3)
        self.assertEqual(ops[0],(6,1,0,3,40,255,0,0,0,0,0,2))
    def test_prepend_owns_actual_rail_part_and_retains_draw(self):
        parts,ops=self.capture('PaintAddImageAsParent(session,session.TrackColours.WithIndex(100),{0,0,height},{32,20,2});session.WoodenSupportsPrependTo=PaintAddImageAsParent(session,session.TrackColours.WithIndex(101),{0,0,height},{32,20,2});WoodenASupportsPaintSetup(session,supportType.wooden,WoodenSupportSubType::neSw,height,session.SupportColours);')
        self.assertEqual([p[0] for p in parts],[100,101])
        self.assertEqual(ops[0][6],1)
        self.assertEqual(ops[0][10:],(2,4))
    def test_runtime_terrain_result_is_not_guessed(self):
        with self.assertRaises(EXTRACTOR.Unsupported):
            self.capture('if(WoodenASupportsPaintSetup(session,supportType.wooden,WoodenSupportSubType::neSw,height,session.SupportColours)){PaintAddImageAsParent(session,session.TrackColours.WithIndex(100),{0,0,height},{32,20,2});}')
    def test_conditional_subtype_array(self):
        _,ops=self.capture('WoodenSupportSubType subtype[]={WoodenSupportSubType::null,WoodenSupportSubType::corner1};if(subtype[direction]!=WoodenSupportSubType::null){WoodenASupportsPaintSetup(session,supportType.wooden,subtype[direction],height,session.SupportColours);}',1)
        self.assertEqual(ops[0][2],3)
    def test_prepend_maps_past_tunnel_metadata_and_rejects_wrong_baseline(self):
        part=(100,0,0,0,0,0,0,1,1,1,0,-1)
        marker=(EXTRACTOR.TUNNEL_PART,)+(0,)*11
        op=(5,255,0,0,40,255,0,0,0,1,1,4)
        self.assertEqual(EXTRACTOR.qualify_wooden_prepend([op],[part],[marker,part])[0][6],1)
        with self.assertRaises(EXTRACTOR.Unsupported): EXTRACTOR.qualify_wooden_prepend([op],[part],[part,part])
    def test_ted_extra_rotation_and_full_enum_range(self):
        self.assertEqual(self.t.constants['WoodenSupportTransitionType_up60DegToFlatLongBaseSeq3'],20)
        found=[]
        for kind in range(350):
            for seq in range(16):
                try:
                    subtype,transition,rotation=self.t.wooden_sequence(kind,seq)
                    if rotation: found.append((kind,seq,rotation))
                except EXTRACTOR.Unsupported: pass
        self.assertTrue(found)


class SupportCompletenessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.t=EXTRACTOR.WoodenSupportTranslator(ROOT)
    def capture(self,style,kind,sequence,direction,height=100,state=0):
        t=self.t;source,name=t.getter(t.getters[style],kind,state=state)
        t.support_ops=[];t.support_gaps=set();t.support_predicate=0;parts=[]
        t.paint(source,name,sequence,direction,height,state,parts,track_type=kind)
        return parts,list(t.support_ops),set(t.support_gaps)
    def test_numeric_c_array_declared_zero_tail_and_signed_values(self):
        source=EXTRACTOR.Source('fixture.cpp','static constexpr int8_t offsets[][7]={{0,0,0,0,0,0,-1},{-1},{0},{}};')
        values=EXTRACTOR.evaluate(EXTRACTOR.tokens('offsets'),source.globals)
        self.assertEqual(values,[[0,0,0,0,0,0,-1],[-1,0,0,0,0,0,0],[0]*7,[0]*7])
        with self.assertRaises(EXTRACTOR.Unsupported):
            EXTRACTOR.evaluate(EXTRACTOR.tokens('offsets[2][7]'),source.globals)
    def test_all_six_previously_rejected_curve_support_programs(self):
        for style in (31,51,78):
            for kind in (16,17):
                for sequence in range(7):
                    for direction in range(4):
                        _,ops,gaps=self.capture(style,kind,sequence,direction)
                        self.assertEqual(ops[-1][0],4)
                        self.assertEqual(ops[-1][4],132)
                        self.assertFalse(gaps)
                        metal=[op for op in ops if op[0] in (1,2)]
                        self.assertEqual(len(metal),int(sequence in ((0,2,5,6) if style==51 else (0,6))))
        # Right turn source direction0/last sequence deliberately lowers its cap1.
        _,ops,_=self.capture(31,17,6,0)
        self.assertEqual(ops[0][4],99)
    def test_helix_supports_use_original_ted_not_reversed_image_indices(self):
        for direction in range(4):
            _,start,gaps=self.capture(2,106,0,direction)
            self.assertFalse(gaps)
            self.assertEqual(start[0][:6],(1,255,4,direction,100,4))
            self.assertEqual(start[-1][4],148)
            _,end,_=self.capture(2,106,6,direction)
            self.assertEqual(end[0][:6],(1,255,4,(direction-1)&3,108,8))
            _,down,_=self.capture(2,108,0,direction)
            self.assertEqual(down[0][:6],(1,255,4,(direction+2)&3,108,8))
        _,left,_=self.capture(2,106,0,0)
        _,right,_=self.capture(2,107,0,0)
        self.assertEqual(left[-2][6],419)
        self.assertEqual(right[-2][6],302)
        for kind in range(102,110):
            for sequence in range(7):
                for direction in range(4):
                    _,ops,gaps=self.capture(2,kind,sequence,direction)
                    self.assertFalse(gaps)
                    self.assertEqual([op[0] for op in ops[-2:]],[3,4])
    def test_station_art_helpers_do_not_report_fictitious_support_omissions(self):
        for style in (1,3,30,39,69):
            _,ops,gaps=self.capture(style,1,0,0,state=64)
            self.assertFalse(gaps)
    def test_photo_platform_columns_and_photo2_state_are_not_swallowed_by_art_adapter(self):
        t=self.t
        for direction in range(4):
            source=EXTRACTOR.Source('fixture.cpp','void Fixture(){TrackPaintUtilOnridePhotoPlatformPaint(session,direction,height,MetalSupportType::tubes);TrackPaintUtilOnridePhotoPaint2(session,direction,trackElement,height,64,7);}')
            t.support_ops=[];t.support_gaps=set();parts=[]
            t.paint(source,'Fixture',0,direction,100,0,parts)
            self.assertEqual([op[0] for op in t.support_ops],[1,1,3,4])
            self.assertEqual([op[2] for op in t.support_ops[:2]],list((6,7) if direction&1 else (5,8)))
            self.assertEqual([op[11] for op in t.support_ops[:2]],[1,1])
            self.assertEqual(t.support_ops[-2][4:8],(65535,0,511,0))
            self.assertEqual(t.support_ops[-1][4],164)
            self.assertEqual(parts[0][0],22432)
            self.assertEqual(parts[1][0],EXTRACTOR.PHOTO_PART)
            self.assertEqual(parts[1][3],107)
        source=EXTRACTOR.Source('fixture.cpp','void Fixture(){TrackPaintUtilOnridePhotoPaint2(session,direction,trackElement,height);}')
        t.support_ops=[];t.paint(source,'Fixture',0,0,100,0,[])
        self.assertEqual(t.support_ops[-1][4],148)
    def test_diagonal_extra_helper_retains_non_owner_direction_supports(self):
        t=self.t
        source=EXTRACTOR.Source('fixture.cpp','void Fixture(){uint32_t sprites[4]={100,101,102,103};TrackPaintUtilDiagTilesPaintExtra(session,1,height,direction,trackSequence,sprites,MetalSupportType::tubes);}')
        for sequence in range(4):
            for direction in range(4):
                t.support_ops=[];t.support_gaps=set();parts=[]
                t.paint(source,'Fixture',sequence,direction,100,0,parts)
                self.assertEqual(len(parts),int(direction==(3,0,2,1)[sequence]))
                self.assertEqual([op[0] for op in t.support_ops],([1] if sequence==3 else [])+[3,4])
                if sequence==3:
                    self.assertEqual(t.support_ops[0][:6],(1,0,1,direction,100,0))

    def test_chairlift_station_owns_neighbor_marker_and_unconditional_support_program(self):
        for kind in (1,2,3):
            for direction in range(4):
                parts,ops,gaps=self.capture(6,kind,0,direction)
                self.assertFalse(gaps)
                self.assertEqual(parts,[(EXTRACTOR.STATION_PART,0,0,100,0,0,0,0,8,0,0,-1)])
                self.assertEqual([op[0] for op in ops],[5,3,4])
                self.assertEqual(ops[0][3],direction)
                self.assertEqual(ops[0][9],2)  # Source station-black support palette.
                self.assertEqual(ops[1][4:8],(65535,0,511,0))
                self.assertEqual(ops[2][4],132)
                source,name=self.t.getter(self.t.getters[6],kind)
                try:
                    EXTRACTOR.CAPTURE_TUNNELS=True
                    tunnel_parts=[]
                    EXTRACTOR.Translator.paint(self.t,source,name,0,direction,100,0,tunnel_parts,track_type=kind)
                    self.assertEqual(tunnel_parts[-1],(EXTRACTOR.TUNNEL_PART,direction&1,6,100,0,0,0,0,0,0,0,-1))
                finally:
                    EXTRACTOR.CAPTURE_TUNNELS=False


if __name__ == '__main__':
    unittest.main()
