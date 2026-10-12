"""Windows loader preflight in isolated hosts; never replaces installed DLLs.

This is an early Phase 7 probe, not a complete dependency-coexistence test. It
records whether ccl_init can be reached after preloading a real OIIO dependency.
"""
import argparse, ctypes, hashlib, json, os, subprocess, sys
from pathlib import Path

class Init(ctypes.Structure):
    _fields_=[('size',ctypes.c_uint32),('resource_path',ctypes.c_char_p),('user_path',ctypes.c_char_p)]

def child(args):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.SetErrorMode(0x0001|0x0002|0x8000)
    kernel.GetModuleFileNameW.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_uint32]
    kernel.GetModuleFileNameW.restype=ctypes.c_uint32
    def filename(handle):
        buffer=ctypes.create_unicode_buffer(32768)
        if not kernel.GetModuleFileNameW(handle,buffer,len(buffer)):raise ctypes.WinError(ctypes.get_last_error())
        return buffer.value
    search=[os.add_dll_directory(str(args.host.resolve())),os.add_dll_directory(str(args.install.resolve()))]
    result={'host':str(args.host.resolve()),'cycles_c':str((args.install/'cycles_c.dll').resolve()),'modules':[], 'init_reached':False}
    retained=[]
    for name in ('OpenImageIO_Util.dll','OpenImageIO.dll'):
        path=args.host/name
        lib=ctypes.WinDLL(str(path.resolve()),winmode=0x1100)
        retained.append(lib)
        actual=filename(lib._handle)
        assert Path(actual).resolve()==path.resolve()
        result['modules'].append(dict(name=name,path=actual,sha256=hashlib.sha256(Path(actual).read_bytes()).hexdigest()))
    try:
        api=ctypes.CDLL(str((args.install/'cycles_c.dll').resolve()),winmode=0x1100)
    except OSError as error:
        result.update(stage='cycles_c_load',winerror=error.winerror,error=str(error),library_loaded=False)
    else:
        result['library_loaded']=True
        result['actual_api']=filename(api._handle)
        api.ccl_init.argtypes=[ctypes.POINTER(Init)];api.ccl_init.restype=ctypes.c_int32
        api.ccl_last_error.restype=ctypes.c_char_p
        p=Init(ctypes.sizeof(Init),str(args.install.resolve()).encode(),str(args.output.parent.resolve()).encode())
        result['init_reached']=True;result['init_status']=int(api.ccl_init(ctypes.byref(p)))
        result['init_error']=api.ccl_last_error().decode('utf-8','replace')
        if not result['init_status']:
            n=ctypes.c_uint32();api.ccl_node_type_count.argtypes=[ctypes.POINTER(ctypes.c_uint32)]
            result['reflection_status']=int(api.ccl_node_type_count(ctypes.byref(n)));result['node_types']=n.value
        result['stage']='init'
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))

def main():
    parser=argparse.ArgumentParser();parser.add_argument('install',type=Path);parser.add_argument('host',type=Path);parser.add_argument('output',type=Path);parser.add_argument('--child',action='store_true');args=parser.parse_args()
    if os.name!='nt':raise SystemExit('This probe checks the Windows loader; other platforms need their own test.')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    if args.child:return child(args)
    command=[sys.executable,str(Path(__file__).resolve()),str(args.install.resolve()),str(args.host.resolve()),str(args.output.resolve()),'--child']
    with args.output.with_suffix('.log').open('w') as log:
        result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=60)
    if result.returncode:raise SystemExit(f'Host process failed: {result.returncode}; inspect {args.output.with_suffix(".log")}')
    print(args.output.read_text())
if __name__=='__main__':main()
