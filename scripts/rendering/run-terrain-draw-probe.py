"""Qualify actual retained terrain drawing with pinned frozen corpus and Vulkan validation."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def stable(value):
    return value.get('status') == 'pass' and not any(value.get(key) for key in (
        'sourceChangesDuringBuild','dependencyChangesDuringBuild','testChangesDuringBuild','sdkChangesDuringBuild'))
def source_hash(receipt, name):
    values = [receipt.get(section, {}).get(key) for section in ('sourceSha256','testSourceSha256')
              for key in (name, 'source/' + name)]
    values = {value for value in values if value is not None}
    if len(values) != 1: raise RuntimeError('Ambiguous/missing source receipt input: ' + name)
    return values.pop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build-receipt','shader-build-receipt','emission-probe-receipt','column-probe-receipt','corpus-summary','output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--timeout-seconds', type=int, default=300)
    args = parser.parse_args()
    output = args.output.resolve()
    if args.timeout_seconds <= 0: raise SystemExit('Timeout must be positive')
    output.mkdir(parents=True, exist_ok=False)
    summary = {'schema': 1, 'status': 'fail', 'failures': [], 'exitCode': None, 'reports': [],
               'validationActivated': False, 'validationMessages': [],
               'scope': 'Actual no-window retained GPU terrain columns/order/indexed pixels. No runtime admission, final UI or Gate P claim.'}
    pinned = {}
    dll_directory = None
    dll_set = []
    def pin(path, expected=None):
        path = path.resolve(strict=True)
        digest = sha(path)
        if expected is not None and digest != expected: raise RuntimeError('Pinned runtime input changed: ' + str(path))
        pinned[str(path)] = digest
        return path
    def load(path):
        return json.loads(pin(path).read_text(encoding='utf-8'))
    def contained(base, name):
        path = (base / name).resolve(strict=True)
        if base not in path.parents: raise RuntimeError('Artifact escaped receipt directory')
        return path
    try:
        pin(Path(__file__))
        build_path = args.build_receipt.resolve(strict=True)
        build = load(build_path)
        if not stable(build) or build.get('windowedParityQualified') is not False:
            raise RuntimeError('Successful source-stable no-window build required')
        for name, digest in build['artifactSha256'].items(): pin(contained(build_path.parent,name),digest)
        executable = pin(build_path.parent / 'bin/tests-no-window.exe',build['artifactSha256']['bin/tests-no-window.exe'])
        dll_directory = executable.parent
        dll_set = sorted(str(p.resolve()) for p in dll_directory.glob('*.dll'))
        for name in dll_set:
            path = Path(name); relative = path.relative_to(build_path.parent).as_posix()
            pin(path,build['artifactSha256'][relative])
        root = Path(build['sourceRoot']).resolve(strict=True)
        shaders = output / 'shaders'; shaders.mkdir()
        shader_build = load(args.shader_build_receipt)
        if not stable(shader_build): raise RuntimeError('Stable graphics shader build required')
        wanted = {'indexed_rect.vert.spv','indexed_rect.frag.spv','indexed_sprite.vert.spv'}
        copied = set()
        for name, digest in shader_build['artifactSha256'].items():
            if Path(name).name not in wanted: continue
            source = contained(root,name)
            if source.name in copied: raise RuntimeError('Ambiguous graphics shader binary')
            source_name = 'data/shaders/vulkan/' + source.name.removesuffix('.spv')
            if source_hash(shader_build,source_name) != source_hash(build,source_name):
                raise RuntimeError('Graphics shader source does not match test binary')
            pin(source,digest); destination = shaders / source.name; shutil.copyfile(source,destination); pin(destination,digest)
            copied.add(source.name)
        if copied != wanted: raise RuntimeError('Missing graphics shaders')
        probe_paths = {}
        for kind, path, builder_name, shader_name in (
            ('emission', args.emission_probe_receipt, 'build-terrain-emission-probe.py', 'terrain_retained_emit.comp.spv'),
            ('columns', args.column_probe_receipt, 'build-terrain-column-probe.py', 'terrain_columns.comp.spv')):
            path = path.resolve(strict=True); receipt = load(path)
            if receipt.get('status') != 'pass' or receipt.get('inputsUnchanged') is not True:
                raise RuntimeError('Stable terrain probe compilation required')
            builder = pin(root / 'scripts/rendering' / builder_name,receipt['builderSha256'])
            values = [ast.literal_eval(node.value) for node in ast.parse(builder.read_text(encoding='utf-8')).body
                      if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id == 'INPUTS' for t in node.targets)]
            if len(values) != 1 or set(values[0]) != set(receipt['inputSha256']):
                raise RuntimeError('Incomplete terrain probe input set')
            for name, digest in receipt['inputSha256'].items():
                if source_hash(build,name) != digest: raise RuntimeError('Probe/test source mismatch: ' + name)
            for name, digest in receipt['artifactSha256'].items(): pin(contained(path.parent,name),digest)
            destination = shaders / shader_name
            shutil.copyfile(path.parent / shader_name,destination)
            pin(destination,receipt['artifactSha256'][shader_name]); probe_paths[kind] = destination
        corpus_path = args.corpus_summary.resolve(strict=True); corpus = load(corpus_path)
        if corpus.get('status') != 'pass' or corpus.get('changedInputs') or len(corpus.get('cases',[])) != 32:
            raise RuntimeError('Frozen fresh-repeat/current exact corpus proof required')
        corpus_dir = corpus_path.parent / 'corpus'
        if {p.name for p in corpus_dir.iterdir()} != set(corpus['corpusSha256']):
            raise RuntimeError('Corpus artifact set changed')
        for name, digest in corpus['corpusSha256'].items(): pin(contained(corpus_dir,name),digest)
        corpus_manifest = json.loads((corpus_dir / 'corpus.json').read_text(encoding='utf-8'))
        if corpus_manifest['cases'] != corpus['cases']: raise RuntimeError('Corpus case order differs')
        command = [str(executable),'--gtest_filter=VulkanRetainedTerrainDrawTest.*','--gtest_output=xml:' + str(output / 'tests.xml')]
        settings = output / 'vk_layer_settings.txt'
        settings.write_text('khronos_validation.validate_sync = true\n'
            'khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n'
            'khronos_validation.log_filename = stdout\n'
            'khronos_validation.report_flags = error,warn\n'
            'khronos_validation.enable_message_limit = false\n',encoding='utf-8'); pin(settings)
        env = {k.upper() if os.name == 'nt' else k:v for k,v in os.environ.items()
               if not k.upper().startswith(('VK_','OPENRCT2_'))}
        diagnostic = {'OPENRCT2_REQUIRE_VULKAN_TESTS':'1','OPENRCT2_TEST_USER_DATA_PATH':str(output / 'profile'),
            'OPENRCT2_VULKAN_SHADER_DIRECTORY':str(shaders),'OPENRCT2_TERRAIN_DRAW_CORPUS':str(corpus_dir),
            'OPENRCT2_TERRAIN_DRAW_ARTIFACTS':str(output / 'samples'),
            'OPENRCT2_TERRAIN_EMISSION_SPV':str(probe_paths['emission']),'OPENRCT2_TERRAIN_COLUMNS_SPV':str(probe_paths['columns']),
            'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation','VK_LOADER_DEBUG':'layer','VK_LAYER_SETTINGS_PATH':str(output)}
        env.update(diagnostic); summary.update({'command':command,'diagnosticEnvironment':diagnostic})
        with (output / 'tests.log').open('wb') as log:
            result = subprocess.run(command,cwd=executable.parent,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=args.timeout_seconds)
        summary['exitCode'] = result.returncode
        lines = (output / 'tests.log').read_text(encoding='utf-8',errors='replace').splitlines()
        summary['validationActivated'] = any('Insert instance layer' in line and 'VK_LAYER_KHRONOS_validation' in line for line in lines)
        summary['validationMessages'] = [line for line in lines if any(marker in line.lower() for marker in
            ('vuid-','sync-hazard','validation error','validation warning','validation performance warning'))]
        cases = list(ET.parse(output / 'tests.xml').getroot().iter('testcase'))
        required_tests = {
            'FrozenColumnsAndActualIndexedPixelsAcrossAllCameraPolicies': ('32', None),
            'ZeroPublicationOnInvalidInputsAndCapacityOverflow': ('10', ('cpuRejections', '2')),
            'ImmutableGenerationsAbandonAndSlotReusePreserveFrozenRaster': ('8', ('abandonedRecordings', '1'))}
        if len(cases) != 3 or {case.get('name') for case in cases} != set(required_tests):
            raise RuntimeError('Exactly three retained drawing tests required')
        for case in cases:
            if (case.get('classname') != 'VulkanRetainedTerrainDrawTest' or case.get('status') != 'run'
                    or any(case.find(tag) is not None for tag in ('failure','error','skipped'))):
                raise RuntimeError('Every retained drawing test must run and pass')
            count, extra = required_tests[case.get('name')]
            props = {v.get('name'):v.get('value') for v in case.findall('./properties/property')}
            if props.get('terrainDrawSamples') != count or (extra and props.get(extra[0]) != extra[1]):
                raise RuntimeError('Incomplete retained drawing test coverage')
        if {p.name for p in (output / 'samples').iterdir()} != {str(i) for i in range(32)} | {'guards', 'generations'}:
            raise RuntimeError('Unexpected terrain sample directory set')
        for name, count in (('guards', 10), ('generations', 8)):
            if {p.name for p in (output / 'samples' / name).iterdir()} != {str(i) for i in range(count)}:
                raise RuntimeError('Unexpected guard/generation sample set')
        originals = set()
        for case_name in corpus_manifest['cases']:
            trace = json.loads((corpus_dir / case_name).read_text(encoding='utf-8'))
            for column in trace['columns']:
                for parent in column['parentsBeforeArrange']:
                    originals.add(parent['image']['index'])
                    originals.update(attached['image']['index'] for attached in parent['attached'])
        sprite_bytes = len(originals) * 96
        all_bytes = 1024 * 32 + 4 * 592 + sprite_bytes
        # Expected upload deltas and failures come from this explicit independent
        # schedule, never from the test's self-reported expected fields.
        schedule = [(str(i), i, 0, all_bytes if i == 0 else 0, 0, 8192, 8192) for i in range(32)]
        errors = [0, 2, 2, 2, 2, 1, 0, 3, 0, 0]
        uploads = [all_bytes, 0, 0, 0, 0, 8192, 8192, 96, sprite_bytes, all_bytes]
        capacities = [(8192,8192),(0,8192),(8192,0),(1,8192),(8192,1)] + [(8192,8192)] * 5
        schedule += [('guards/' + str(i), 0, errors[i], uploads[i], 0, *capacities[i]) for i in range(10)]
        uploads = [all_bytes, 8192, 4*592, sprite_bytes, all_bytes, all_bytes, 0, 0]
        schedule += [('generations/' + str(i), 31 if i == 6 else 0, 0, uploads[i], i % 2, 8192, 8192) for i in range(8)]
        for sample, reference_index, expected_error, uploaded, frame_index, parent_capacity, command_capacity in schedule:
            case_name = corpus_manifest['cases'][reference_index]
            folder = output / 'samples' / sample; report = json.loads((folder / 'indexed/report.json').read_text(encoding='utf-8'))
            expected = json.loads((corpus_dir / case_name).read_text(encoding='utf-8'))
            frozen = bytes.fromhex(expected['indexedHex']) if expected_error == 0 else bytes(960*640)
            label = 'frozen-software' if expected_error == 0 else 'required-empty'
            if (report['differingPixels'] != 0 or report['acceptedExceptions'] != [] or report['runtimeAdmission'] is not False
                    or report['width'] != 960 or report['height'] != 640
                    or (folder / ('indexed/' + label + '.bin')).read_bytes() != frozen
                    or (folder / 'indexed/vulkan.bin').read_bytes() != frozen
                    or report['uploadedWorldBytes'] != uploaded or report['expectedUploadBytes'] != uploaded
                    or report['referenceCase'] != reference_index or report['frameIndex'] != frame_index
                    or report['expectedGpuError'] != expected_error or report['parentCapacity'] != parent_capacity
                    or report['commandCapacity'] != command_capacity):
                raise RuntimeError('Frozen indexed raster or retention differs: ' + case_name)
            columns = (folder / 'columns.bin').read_bytes()
            parents_data = (folder / 'parents.bin').read_bytes()
            commands_data = (folder / 'commands.bin').read_bytes()
            before = (folder / 'before.bin').read_bytes()
            if (len(columns) != 65*32 or len(parents_data) != 65*8192*64 or len(commands_data) != 65*8192*60
                    or len(before) != len(columns) + len(parents_data) + len(commands_data)
                    or before != b'\xa5' * len(before)):
                raise RuntimeError('Malformed terrain readback ABI sizes')
            maximum_error = 0
            for column_index in range(65):
                error, parents, commands, head, vertices, instances, first_vertex, first_instance = struct.unpack_from('<IIIiIIII',columns,column_index*32)
                if column_index >= len(expected['columns']):
                    if columns[column_index*32:(column_index+1)*32] != b'\xa5'*32:
                        raise RuntimeError('Inactive column guard overwritten')
                    parents = commands = 0
                else:
                    maximum_error = max(maximum_error,error)
                    if (parents > parent_capacity or commands > command_capacity or vertices != 4 or first_vertex or first_instance
                            or instances != (commands if expected_error == 0 else 0)
                            or (expected_error == 0 and (error or parents != len(expected['columns'][column_index]['parentsBeforeArrange'])))):
                        raise RuntimeError('Terrain column/indirect status differs')
                for data, count, stride in ((parents_data,parents,64),(commands_data,commands,60)):
                    tail = data[(column_index*8192+count)*stride:(column_index+1)*8192*stride]
                    if tail != b'\xa5'*len(tail): raise RuntimeError('Terrain buffer tail guard overwritten')
            if maximum_error != expected_error: raise RuntimeError('Expected GPU rejection was not observed')
            summary['reports'].append({'case':case_name,'sample':sample,'metadata':report,
                'artifactSha256':{p.relative_to(folder).as_posix():sha(p) for p in sorted(folder.rglob('*')) if p.is_file()}})
    except Exception as error:
        summary['failures'].append(type(error).__name__ + ': ' + str(error))
    finally:
        changes = []
        for name,digest in pinned.items():
            try: current = sha(Path(name))
            except OSError: current = None
            if current != digest: changes.append(name)
        if dll_directory and sorted(str(p.resolve()) for p in dll_directory.glob('*.dll')) != dll_set: changes.append('runtime DLL set')
        summary['runtimeInputSha256'] = pinned; summary['changedInputs'] = changes
        for name in ('tests.xml','tests.log'):
            path = output / name; summary[name + 'Sha256'] = sha(path) if path.is_file() else None
        if (summary['exitCode'] == 0 and not summary['failures'] and not changes and summary['validationActivated']
                and not summary['validationMessages'] and len(summary['reports']) == 50): summary['status'] = 'pass'
        (output / 'summary.json').write_text(json.dumps(summary,indent=2) + '\n',encoding='utf-8')
        print(json.dumps({'status':summary['status'],'samples':len(summary['reports']),'failures':summary['failures']}))
    raise SystemExit(0 if summary['status'] == 'pass' else 1)


if __name__ == '__main__': main()
