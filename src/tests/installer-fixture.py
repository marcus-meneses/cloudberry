import importlib.util,json,pathlib,sys
root=pathlib.Path(__file__).resolve().parents[1];build=pathlib.Path(sys.argv[1]);work=build/'tests/installer';work.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('bundle',root/'tools/build-app-bundle.py');tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
manifest=work/'manifest.json';manifest.write_text(json.dumps(dict(version=1,resources=[dict(name='/@streams',operations=['read','write']),dict(name='/@filesystem',operations=['open','stat']),dict(name='/@applications',operations=['data','resources'])])))
resources=work/'resources';(resources/'sub').mkdir(parents=True,exist_ok=True);(resources/'sub/hello.txt').write_text('resource payload\n')
tool.build('sample','112233445566778899aabbccddeeff00',build/'tests/apps/installed-sample.elf',manifest,resources,sys.argv[2],work/'sample.cbp')
tool.build('shared','2233445566778899aabbccddeeff0011',build/'tests/apps/installed-sample.elf',manifest,resources,sys.argv[2],work/'shared.cbp')
(resources/'sub/hello.txt').write_text('upgraded payload\n')
tool.build('sample','112233445566778899aabbccddeeff00',build/'tests/apps/installed-sample-v2.elf',manifest,resources,sys.argv[2],work/'sample-v2.cbp',revision=2)
policy=json.loads(manifest.read_text());policy['resources'][1]['operations'].append('mkdir');manifest.write_text(json.dumps(policy));(work/'approved-full.json').write_text(json.dumps(policy))
tool.build('sample','112233445566778899aabbccddeeff00',build/'tests/apps/installed-sample-v2.elf',manifest,resources,sys.argv[2],work/'sample-v3.cbp',revision=3)
(work/'approved.json').write_text('{"version":1,"resources":[{"name":"/@streams","operations":["read"]}]}')
valid=(work/'sample.cbp').read_bytes();bad=bytearray(valid);bad[-1]^=1;(work/'bad.cbp').write_bytes(bad)
public=(build/'installer-trusted-key.bin').read_bytes();assert tool.verify_bundle(valid,public)['name']=='sample'
try:tool.verify_bundle(bytes(bad),public)
except ValueError:pass
else:raise AssertionError('invalid signature accepted')
print('PASS installer signed fixture: host verification and modified-signature refusal')

policy["resources"][1]["operations"]=["open"]
(work/"maintenance-approved.json").write_text(json.dumps(policy))
