"""Exercise every independent rule-46 selection without launching a game."""
from pathlib import Path
import json, subprocess, sys
root=Path(__file__).resolve().parent.parent
exe=root/'build/native/bin/Release/xml2_shared_handlers_c_test.exe'
out=root/'build/handlers-c-matrix';out.mkdir(parents=True,exist_ok=True)
results=[]
for mask in range(1,2048):
    run=subprocess.run([str(exe),*sys.argv[1:3],str(mask)],capture_output=True,text=True,creationflags=0x08004000,timeout=120)
    (out/f'{mask:03x}.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    results.append({'mask':mask,'exit':run.returncode,'last':run.stdout.strip().splitlines()[-1:]})
    if run.returncode: print(mask,run.returncode,run.stdout[-400:],flush=True)
(out/'results.json').write_text(json.dumps(results,indent=1),encoding='utf-8')
failed=sum(x['exit']!=0 for x in results)
print(f'Rule 46 selection matrix: {len(results)} runs, {failed} failures',flush=True)
raise SystemExit(bool(failed))
