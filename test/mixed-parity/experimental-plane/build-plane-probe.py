"""Compile the staged diagnostic plane overlay against a qualified host receipt."""
import argparse,hashlib,importlib.util,json,subprocess
from pathlib import Path
STAGE=Path(__file__).resolve().parent
ROOT=next(p for p in Path(__file__).resolve().parents if (p / 'scripts/rendering/run-mixed-parity.py').is_file() and (p / 'src/openrct2').is_dir())
BASE=ROOT/'scripts/rendering/build-mixed-emission-probe.py'
s=importlib.util.spec_from_file_location('base_mixed_builder',BASE);base=importlib.util.module_from_spec(s);s.loader.exec_module(base)
NAMES=('mixed_fixture_emit.comp','indexed_sprite.vert','indexed_rect.vert','indexed_rect.frag')
INPUTS=tuple(dict.fromkeys(base.INPUTS+tuple(('test/mixed-parity/shaders/' if n=='mixed_fixture_emit.comp' else 'data/shaders/vulkan/')+n for n in NAMES)
                        +('src/openrct2-renderer/vulkan/VulkanResources.cpp',)))
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--test-build-receipt',type=Path,required=True);p.add_argument('--glslc',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--timeout-seconds',type=int,default=120)
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    report={'schema':1,'status':'fail','model':'plane-envelope-v1','failures':[],'commands':[]};pins={}
    def pin(path,expected=None):
        path=Path(path).resolve(strict=True);h=base.sha(path)
        if expected is not None and h!=expected:raise RuntimeError('Input changed: '+str(path))
        pins[str(path)]=h;return h
    compiler=None;dlls=[]
    try:
        if args.timeout_seconds<=0:raise RuntimeError('Positive timeout required')
        rp=args.test_build_receipt.resolve(strict=True);rh=pin(rp);build=json.loads(rp.read_text())
        if not base.stable_receipt(build):raise RuntimeError('Qualified host build required')
        host={n:pin(ROOT/n,base.receipt_input(build,n)) for n in INPUTS}
        manifest=json.loads((STAGE/'overlay-sources.json').read_text());pin(STAGE/'overlay-sources.json')
        pin(STAGE/'stage-shaders.py',manifest['generatorSha256']);pin(BASE);pin(__file__)
        for n in NAMES:
            if manifest['baseShaderSha256'][n]!=host[('test/mixed-parity/shaders/' if n=='mixed_fixture_emit.comp' else 'data/shaders/vulkan/')+n]:raise RuntimeError('Overlay generated from another base: '+n)
            pin(STAGE/n,manifest['overlaySha256'][n])
        # Fragment-depth writes need both stages in the clear/render-pass dependency.
        resources=(ROOT/'src/openrct2-renderer/vulkan/VulkanResources.cpp').read_text()
        rect=(ROOT/'src/openrct2-renderer/vulkan/VulkanRectPipeline.cpp').read_text()
        test=(ROOT/'test/tests/VulkanMixedFixtureTests.cpp').read_text()
        if 'VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT' not in resources:
            raise RuntimeError('Apply the staged late-depth clear dependency before host build')
        if '.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT\n                    | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT' not in rect:
            raise RuntimeError('Apply the staged late-depth subpass dependency before host build')
        if 'OPENRCT2_MIXED_DEPTH_MODEL' not in test:raise RuntimeError('Host does not label the diagnostic depth model')
        compiler=args.glslc.resolve(strict=True);pin(compiler);dlls=sorted(str(x.resolve()) for x in compiler.parent.glob('*.dll'))
        for n in dlls:pin(n)
        report.update({'testBuildReceipt':str(rp),'testBuildReceiptSha256':rh,'inputSha256':host,
                       'overlaySha256':manifest['overlaySha256'],'overlayDirectory':str(STAGE),'builderSha256':base.sha(__file__)})
        with (out/'build.log').open('wb') as log:
            for n in NAMES:
                cmd=[str(compiler),'-I',str(ROOT/'data/shaders/vulkan'),str(STAGE/n),'-o',str(out/(n+'.spv'))]
                result=subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=args.timeout_seconds)
                report['commands'].append({'command':cmd,'exitCode':result.returncode})
                if result.returncode:raise RuntimeError('Shader compile failed: '+n)
                data=(out/(n+'.spv')).read_bytes()
                if len(data)<20 or len(data)%4 or data[:4]!=b'\x03\x02\x23\x07':raise RuntimeError('Invalid SPIR-V: '+n)
    except Exception as e:report['failures'].append(type(e).__name__+': '+str(e))
    finally:
        changed=[n for n,h in pins.items() if not Path(n).is_file() or base.sha(n)!=h]
        if compiler and dlls!=sorted(str(x.resolve()) for x in compiler.parent.glob('*.dll')):changed.append('Compiler DLL set')
        report.update({'runtimeInputSha256':pins,'changedInputs':changed,'inputsUnchanged':not changed,
                       'artifactSha256':{x.name:base.sha(x) for x in out.iterdir() if x.is_file()}})
        report['status']='pass' if not changed and not report['failures'] and len(report['commands'])==4 else 'fail'
        (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(report['status'])
    raise SystemExit(0 if report['status']=='pass' else 1)
if __name__=='__main__':main()
