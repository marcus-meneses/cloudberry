import copy,importlib.util,json,pathlib,subprocess,sys,tempfile,unittest
source=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('builder',source/'tools/build-installations.py');builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
binary=pathlib.Path(sys.argv.pop(1)).resolve();work=pathlib.Path(sys.argv.pop(1)).resolve();work.mkdir(parents=True,exist_ok=True)
class Catalog(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=work);self.root=pathlib.Path(self.temp.name)
        self.records=copy.deepcopy(json.loads((source/'apps/installed.json').read_text()))
        for r in self.records:
            path=self.root/r['manifest'];path.parent.mkdir(parents=True,exist_ok=True);path.write_text('{"version":1,"resources":[]}')
            program=self.root/r['name']/(r['name']+'.elf');program.touch()
    def tearDown(self):self.temp.cleanup()
    def build(self):
        p=self.root/'installed.json';p.write_text(json.dumps(self.records));builder.build(p,self.root,self.root/'installed.bin')
    def test_contract(self):self.build();subprocess.run([str(binary),str(self.root/'installed.bin')],check=True)
    def test_invalid_bindings(self):
        original=copy.deepcopy(self.records)
        for change in ('identity','name','reserved_name','path','manifest','field','manager_cpu'):
            self.records=copy.deepcopy(original)
            if change=='identity':self.records[1]['installation_id']=self.records[0]['installation_id']
            if change=='name':self.records[1]['name']=self.records[0]['name']
            if change=='reserved_name':self.records[1]['name']='_index'
            if change=='path':self.records[1]['executable']='/system/kmodules/drivers/../escape'
            if change=='manifest':self.records[1]['manifest']='../escape'
            if change=='field':self.records[1]['override_manifest']='evil'
            if change=='manager_cpu':next(r for r in self.records if r['flags']==4)['cpu']=1
            with self.assertRaises(ValueError):self.build()
    def test_privilege_ceiling(self):
        for grant in [dict(name='/@applications',publish=True,operations=['launch']),dict(name='/@io',scope='process-manager',operations=['process_launch']),dict(name='/@io',scope='keyboard',operations=['process_launch']),dict(name='/@io',scope='hid',operations=['hid_subscribe'])]:
            (self.root/next(r for r in self.records if r['flags']==1)['manifest']).write_text(json.dumps(dict(version=1,resources=[grant])))
            with self.assertRaises(ValueError):self.build()
if __name__=='__main__':unittest.main()
