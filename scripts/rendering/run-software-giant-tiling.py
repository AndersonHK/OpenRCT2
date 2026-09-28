"""Diagnostic only: unchanged current X8 full viewport versus production 2048 tile geometry.
Uses receipt-qualified libraries and explicit giant-input-01 r2z0 camera; no GPU path.
Tiled divergence is a reported experimental result, never a parity exception.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import traceback
from PIL import Image, ImageChops

HELPER = Path(__file__).with_name('run-giant-screenshot-parity.py')
spec = importlib.util.spec_from_file_location('giant', HELPER)
giant = importlib.util.module_from_spec(spec)
spec.loader.exec_module(giant)
base = giant.base
require, read, sha, pin = base.require, base.read, base.sha, base.pin
NAMES = ('ordinary-r2z0', 'transparent-r2z0')


def difference(left, right, extent, folder, name):
    require(len(left) == len(right) == extent[0] * extent[1], 'Indexed extent differs')
    delta = ImageChops.difference(Image.frombytes('L', tuple(extent), left), Image.frombytes('L', tuple(extent), right))
    count = len(left) - delta.histogram()[0]
    result = {'differingPixels': count, 'bounds': delta.getbbox(), 'manualReview': 'pending' if count else 'no-divergence'}
    if count:
        delta.save(folder / (name + '-difference.png'))
        x0, y0, x1, y1 = delta.getbbox()
        box = (max(0, x0-24), max(0, y0-24), min(extent[0], x1+24), min(extent[1], y1+24))
        for label in ('full', 'tiled'):
            with Image.open(folder / 'capture' / label / 'screen.png') as image:
                image.convert('RGBA').crop(box).save(folder / (name + '-' + label + '-crop.png'))
        result['sampleBox'] = box
    return result


def execute(args, root, output, summary, evidence):
    for path in (Path(__file__), HELPER, base.__file__, root/'scripts/rendering/verify-software-reference.py'):
        evidence.track(Path(path))
    with (output/'reference-verification.log').open('w', encoding='utf-8') as log:
        verified = subprocess.run([sys.executable, str(root/'scripts/rendering/verify-software-reference.py')],
                                  stdout=log, stderr=subprocess.STDOUT, timeout=180)
    require(verified.returncode == 0, 'Frozen reference verification failed')
    reference_path = root/'docs/vulkan-software-reference.json'
    reference = read(reference_path)
    park, fixture, cameras = giant.verify_fixture(args.fixture_manifest, root, evidence, args.fixture_source_archive)
    require(cameras['r2z0'] == {'viewPosition': [-3008, -3536], 'extent': [6016, 3488]}, 'Explicit diagnostic camera differs from accepted fixture')
    extent = cameras['r2z0']['extent']
    data = root/reference['localReference']/'package/data'
    data_hashes = evidence.tree(data)
    require(data_hashes == fixture['assetSha256']['data'], 'Installed assets differ from accepted fixture')
    assets = {name: evidence.track(folder.resolve(strict=True)/'Data'/name)
              for folder, names in ((args.rct1, ('csg1.dat','csg1i.dat')), (args.rct2, ('g1.dat',))) for name in names}
    for key, path in (('rct1',args.rct1),('rct2',args.rct2)):
        require(evidence.tree(path) == fixture['assetSha256'][key], 'Original asset tree differs: '+key)
    provenance = args.build_receipt.resolve(strict=True)
    receipt = read(provenance)
    require(receipt.get('status') == 'pass' and receipt.get('exitCode') == 0, 'Passing diagnostic driver build required')
    giant.verify_configured_build(receipt, provenance, root, evidence)
    require('harness/test/cli-parity/SoftwareTileDiagnostic.h' in receipt['sourceSha256'], 'Build lacks the diagnostic header')
    require('bin/screenshot-parity.exe' in receipt['artifactSha256'], 'Build receipt does not pin the diagnostic executable')
    for name, digest in receipt['artifactSha256'].items(): evidence.track(base.child(provenance.parent,name),digest)
    runtime = evidence.tree(provenance.parent/'bin','*.dll',require_nonempty=False)
    require(runtime == receipt['runtimeDllSha256'], 'Runtime DLL inventory differs')
    executable = provenance.parent/'bin/screenshot-parity.exe'
    identity = {'park':evidence.track(park),'assets':assets,'referenceReceipt':evidence.track(reference_path),
                'dataSha256':data_hashes,'fixture':{'kind':'giant-seams-v1','version':1,'caseNames':list(giant.CASE_NAMES),
                'inputManifest':evidence.track(args.fixture_manifest.resolve(strict=True)), 'cameras':cameras,
                'backgroundPolicy':'config-false-explicit-cli-switch'}}
    summary.update(identity)
    summary.update({'buildReceipt':evidence.track(provenance),'executable':evidence.track(executable),'runtimeDllSha256':runtime})
    references = []
    for path in args.compare_run:
        path = path.resolve(strict=True)
        previous, receipt_pin = giant.verify_reference(path, identity, evidence)
        require(previous.get('freshRepeat') is True and previous['renderer'] in ('frozen','software'), 'Use frozen/current software fresh-repeat references')
        references.append((path, previous))
        summary['comparisonReceipts'].append(receipt_pin)
    require({p['renderer'] for _,p in references} == {'frozen','software'}, 'Both frozen02/current-software02 references are required')
    env = {key.upper() if os.name == 'nt' else key:value for key,value in os.environ.items()
           if not key.upper().startswith(('OPENRCT2_','VK_'))}
    env.update({'OPENRCT2_SOFTWARE_TILE_DIAGNOSTIC':'1','OPENRCT2_ORACLE_DATA_PATH':str(data),
                'OPENRCT2_ORACLE_RCT1_PATH':str(args.rct1.resolve()),'OPENRCT2_ORACLE_RCT2_PATH':str(args.rct2.resolve())})
    env['OPENRCT2_SOFTWARE_TILE_FULL_GENERATION'] = '1' if args.full_generation else '0'
    config = '[general]\nrct1_path = '+base.ini(args.rct1.resolve())+'\ngame_path = '+base.ini(args.rct2.resolve())+'\ntransparent_screenshot = false\ndrawing_engine = "SOFTWARE_HWD"\n'
    for name in NAMES:
        folder=output/name;folder.mkdir();profile=folder/'profile';profile.mkdir()
        (profile/'config.ini').write_text(config,encoding='utf-8')
        (folder/'config-input.ini').write_text(config,encoding='utf-8');evidence.track(folder/'config-input.ini')
        env.update({'OPENRCT2_ORACLE_USER_PATH':str(profile),'OPENRCT2_CLI_PARITY_ARTIFACTS':str(folder/'capture')})
        command=[str(executable),'screenshot',str(park),str(folder/'unused-output.png'),'giant','0','2']
        if name.startswith('transparent'):command.append('--transparent')
        case={'name':name,'command':command,'failures':[],'exitCode':None,'comparisons':[]}
        summary['cases'].append(case)
        try:
            with (folder/'capture.log').open('w',encoding='utf-8') as log:
                completed=subprocess.run(command,cwd=executable.parent,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=args.timeout)
            case['exitCode']=completed.returncode
            require(completed.returncode==0,'Software diagnostic failed')
            report=read(folder/'capture/report.json'); observation=report['softwareTiling']
            require(report['exitCode']==0 and report['error']=='' and report['serviceCreations']==report['deviceCreations']==0
                    and report['fixture']=='software-giant-tiling-r2z0','Unexpected diagnostic/service result')
            require(observation.get('generationDomain') == ('fullViewportHeight' if args.full_generation else 'targetClip'), 'Generation domain differs')
            require(observation['renderer']=='unchanged-linked-X8DrawingEngine' and observation['configuredEngine']==0
                    and observation['fullAfterTiledExact'] is True and observation['tickBefore']==observation['tickAfter'],
                    'Software renderer/configuration/state stability differs')
            require(observation['camera']['viewPosition']==cameras['r2z0']['viewPosition']
                    and observation['camera']['extent']==extent and observation['camera']['position']==[0,0]
                    and observation['camera']['rotation']==2 and observation['camera']['zoom']==0
                    and observation['transparent']==name.startswith('transparent') and observation['tileSide']==2048,
                    'Camera/background/tile policy differs')
            require(observation['assetState']['csgLoaded'] is True and observation['assetState']['g1Records']==29294
                    and observation['assetState']['g1Payloads']>0,'Required loaded assets absent')
            buffers={}
            for label in ('full','tiled'):
                location=folder/'capture'/label
                buffers[label]=giant.png_buffers(location/'screen.png',extent)
                for filename,content in buffers[label].items():require((location/filename).read_bytes()==content,'PNG/raw diagnostic differs')
            require(all(buffers['full'][key]==buffers['tiled'][key] for key in ('palette.bin','alpha.bin')),
                    'Full/tiled palette or alpha tables differ')
            expected_tiles=[(x,y,min(2048,extent[0]-x),min(2048,extent[1]-y)) for y in range(0,extent[1],2048) for x in range(0,extent[0],2048)]
            require(len(observation['tiles'])==len(expected_tiles),'Tile inventory differs')
            assembled=bytearray(len(buffers['tiled']['indexed.bin']))
            for ordinal,((x,y,width,height),tile) in enumerate(zip(expected_tiles,observation['tiles'])):
                expected_camera=dict(observation['camera']);expected_camera['position']=[-x,-y]
                require(tile['ordinal']==ordinal and tile['bounds']==[x,y,width,height] and tile['camera']==expected_camera
                        and tile['tick']==observation['tickBefore'],'Tile phase/order/tick differs')
                tile_path=base.child(folder/'capture',tile['indexed']);pixels=tile_path.read_bytes()
                require(len(pixels)==width*height,'Tile raw extent differs')
                for row in range(height):assembled[(y+row)*extent[0]+x:(y+row)*extent[0]+x+width]=pixels[row*width:(row+1)*width]
            require(assembled==buffers['tiled']['indexed.bin'],'Owned tile assembly differs')
            case['fullVsTiled']=difference(buffers['full']['indexed.bin'],buffers['tiled']['indexed.bin'],extent,folder,'full-vs-tiled')
            require(case['fullVsTiled']['differingPixels']==observation['fullTiledDifferentPixels'],'Native/recomputed difference counts disagree')
            if args.full_generation:require(case['fullVsTiled']['differingPixels']==0,'Whole-height generation did not restore exact software pixels')
            for path, previous in references:
                differences={filename:int(content!=(path/name/filename).read_bytes()) for filename,content in buffers['full'].items()}
                require(not any(differences.values()),'Full software does not match qualified reference: '+str(path))
                case['comparisons'].append({'reference':str(path),'fullExact':True,'tiledDifferentPixels':case['fullVsTiled']['differingPixels']})
            case['observation']=observation
        except subprocess.TimeoutExpired:
            case['failures'].append('Diagnostic timed out; partial outputs retained')
        except Exception as error:case['failures'].append(type(error).__name__+': '+str(error))
        finally:
            case['artifacts']={p.relative_to(folder).as_posix():pin(p) for p in folder.rglob('*')
                               if p.is_file() and 'profile' not in p.relative_to(folder).parts}
            summary['failures'].extend(name+': '+failure for failure in case['failures'])
            base.write_summary(output,summary,False)
    require(len(summary['cases'])==len(NAMES),'Diagnostic case inventory incomplete')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('output','build-receipt','fixture-manifest','rct1','rct2'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--fixture-source-archive',type=Path)
    parser.add_argument('--compare-run',type=Path,action='append',required=True)
    parser.add_argument('--timeout',type=int,default=300)
    parser.add_argument('--full-generation',action='store_true',help='Explicit corrected domain; original diagnostic remains default')
    args=parser.parse_args();root=Path(__file__).resolve().parents[2];output=args.output.resolve()
    require(0<args.timeout<=900 and not output.exists() and root in output.parents,'New workspace output and bounded timeout required')
    output.mkdir(parents=True);evidence=base.Evidence()
    summary={'schema':1,'kind':'software-giant-tiling-diagnostic','status':'incomplete','cases':[],'failures':[],
             'comparisonReceipts':[],'manualReview':'pending','parityClaim':False,'scope':__doc__}
    base.write_summary(output,summary,False)
    completed=False
    try:
        execute(args,root,output,summary,evidence)
        completed=True
    except Exception as error:
        summary['failures'].append(type(error).__name__+': '+str(error))
        (output/'runner-error.log').write_text(traceback.format_exc(),encoding='utf-8')
    finally:
        if not completed and not summary['failures']:summary['failures'].append('Diagnostic runner interrupted before completion')
        try:
            summary['inputAuditFailures']=evidence.audit();summary['failures'].extend(summary['inputAuditFailures'])
        except Exception as error:summary['failures'].append('Input audit failed: '+str(error))
        summary['inputSha256']=evidence.files
        base.write_summary(output,summary,True)
    print(json.dumps({'status':summary['status'],'cases':len(summary['cases']),'output':str(output)}))
    raise SystemExit(bool(summary['failures']))

if __name__=='__main__':main()
