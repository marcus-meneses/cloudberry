#!/usr/bin/env python3
"""Alternative images must publish the same versioned frontend contract."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'kmodules'
for family in ('cpu','iommu'):
    alternatives=[]
    for vendor in ('intel','amd'):
        spec=json.loads((root/vendor/f'{family}-{vendor}.json').read_text())
        assert spec['name']==family+'-backend'
        contract={(entry['name'],tuple(entry.get('version',[1,0]))) for entry in spec['exports']}
        assert len(contract)==len(spec['exports'])
        alternatives.append(contract)
    assert alternatives[0]==alternatives[1],family+' backend contract drift'
print('PASS contracts: Intel/AMD alternatives publish identical versioned CPU/IOMMU frontends')
