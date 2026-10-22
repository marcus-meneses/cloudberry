"""Parse the production monitor's text records; percentages use one logical CPU."""
import re
ROW=re.compile(r'^\s*(\d+)\s+(\d+)\s+(\S+)\s+(--|\d+\.\d)\s+(\d+)\s+(\d+\.\d{3})\s+(\S+)\s*$')
def rows(text):
    result=[]
    for line in text.splitlines():
        match=ROW.match(line)
        if match:
            pid,cpu,state,usage,memory,time,name=match.groups()
            result.append(dict(pid=int(pid),cpu=int(cpu),state=state,cpu_percent=None if usage=='--' else float(usage),user_kib=int(memory),cpu_ms=round(float(time)*1000),name=name))
    return result

def summaries(records):
    import statistics
    names=sorted({r['name'] for r in records})
    return {name:dict(cpu_percent_median=statistics.median([r['cpu_percent'] for r in records if r['name']==name and r['cpu_percent'] is not None]),samples=sum(r['name']==name and r['cpu_percent'] is not None for r in records)) for name in names if any(r['name']==name and r['cpu_percent'] is not None for r in records)}
