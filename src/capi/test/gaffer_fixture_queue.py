"""Fresh guarded small-scene qualification, never a retry or reference rewrite."""
import json, os, subprocess, sys
sys.dont_write_bytecode=True
from workspace_guard import output, environment, REPO, disk, self_test
self_test()
root=output(sys.argv[1]);root.mkdir(exist_ok=False)
os.environ.update(environment(root/'temp'))
device=sys.argv[2]
for fixture in ('emission','meshes','point-light'):
    cases=[('native', 'Cycles',1),('api','CyclesC',1)] if device=='CPU' else (
        [('native-'+str(i),'Cycles',1) for i in range(1,6)]+
        [('seed-'+str(i),'Cycles',100+i) for i in range(1,5)]+[('api','CyclesC',1)])
    for label,backend,seed in cases:
        disk()
        target=output(root/(fixture+'-'+label))
        env=environment(root/'temp');env['CAPI_TEST_SEED']=str(seed)
        args=[sys.executable,'-B',str(REPO/'src/capi/test/gaffer_run_scene.py'),str(target),backend,fixture,device]
        output(root/'state.json').write_text(json.dumps(dict(fixture=fixture,case=label,command=args)))
        result=subprocess.run(args,env=env,timeout=300)
        if result.returncode: raise SystemExit(result.returncode)
output(root/'state.json').write_text(json.dumps(dict(status='renders-complete',device=device)))
