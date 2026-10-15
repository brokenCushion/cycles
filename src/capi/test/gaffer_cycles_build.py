"""Configure Cycles against only the approved Gaffer dependency prefix."""
import json
import os
import subprocess
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, environment, cmake_paths, disk, REPO

root = output(sys.argv[1] if len(sys.argv)>1 else 'D:/temp/cycles_c_api/phase9-matched-cycles')
root.mkdir(exist_ok=False)
dep = output('D:/temp/cycles_c_api/phase9-package/extracted/gafferDependencies-11.0.0a8-windows-platform25')
assert json.loads((dep.parent.parent/'verified.json').read_text())['verified_before_extraction']
env = environment(root/'temp')
env['PATH'] = str(dep/'bin') + ';' + str(dep/'lib') + ';' + env['PATH']
disk()
build, install = output(root/'build'), output(root/'install')
args = json.loads((REPO/'src/capi/test/windows_cpu_config.json').read_text())
args = [s.replace('@BUILD@',str(build)).replace('@INSTALL@',str(install)) for s in args]
replace = {'WITH_LIBS_PRECOMPILED':'OFF', 'WITH_CYCLES_DEEP_OPAQUE':'OFF',
           'WITH_CYCLES_OPENIMAGEDENOISE':'OFF'}
if '--gpu' in sys.argv:
    replace.update(WITH_CYCLES_DEVICE_CUDA='ON', WITH_CYCLES_DEVICE_OPTIX='ON',
                   WITH_CYCLES_CUDA_BINARIES='ON')
if '--deep' in sys.argv:
    replace['WITH_CYCLES_DEEP_OPAQUE'] = 'ON'
args = [s for s in args if not any(s.startswith('-D'+k+'=') for k in replace)]
args += ['-D'+k+'='+v for k,v in replace.items()]
args += ['-DCMAKE_PREFIX_PATH='+dep.as_posix(), '-DOSL_ROOT_DIR='+dep.as_posix(),
         '-DOPENIMAGEIO_ROOT_DIR='+dep.as_posix(), '-DOPENVDB_ROOT_DIR='+dep.as_posix(),
         '-DOPENCOLORIO_ROOT_DIR='+dep.as_posix(), '-DEMBREE_ROOT_DIR='+dep.as_posix(),
         '-DTBB_ROOT_DIR='+dep.as_posix(), '-DPUGIXML_ROOT_DIR='+dep.as_posix()]
# The release omits OIIO command-line tools but its config imports them eagerly.
# Import the two shipped libraries only; no fake executables or altered binaries.
shim = output(root/'oiio-config')
shim.mkdir()
config = 'find_package(Imath REQUIRED CONFIG)\n'
for name in ('OpenImageIO_Util','OpenImageIO'):
    dll, lib = dep/'bin'/(name+'.dll'), dep/'lib'/(name+'.lib')
    assert dll.is_file() and lib.is_file()
    config += f'''if(NOT TARGET OpenImageIO::{name})
add_library(OpenImageIO::{name} SHARED IMPORTED)
set_target_properties(OpenImageIO::{name} PROPERTIES
IMPORTED_LOCATION "{dll.as_posix()}" IMPORTED_IMPLIB "{lib.as_posix()}"
INTERFACE_INCLUDE_DIRECTORIES "{dep.as_posix()}/include")
endif()
'''
config += 'set_property(TARGET OpenImageIO::OpenImageIO PROPERTY INTERFACE_LINK_LIBRARIES OpenImageIO::OpenImageIO_Util)\n'
output(shim/'OpenImageIOConfig.cmake').write_text(config)
output(shim/'OpenImageIOConfigVersion.cmake').write_bytes((dep/'lib/cmake/OpenImageIO/OpenImageIOConfigVersion.cmake').read_bytes())
args += ['-DOpenImageIO_DIR='+shim.as_posix()]
# Gaffer ships OIIO's namespaced/header-only pugixml rather than a standalone lib.
args += ['-DPUGIXML_INCLUDE_DIR='+str(dep/'include/OpenImageIO/detail/pugixml'),
         '-DPUGIXML_LIBRARY='+str(dep/'lib/OpenImageIO.lib')]
ocio = output(root/'ocio-config')
ocio.mkdir()
assert (dep/'bin/OpenColorIO_2_4.dll').is_file() and (dep/'lib/OpenColorIO.lib').is_file()
output(ocio/'OpenColorIOConfig.cmake').write_text(f'''if(NOT TARGET OpenColorIO::OpenColorIO)
add_library(OpenColorIO::OpenColorIO SHARED IMPORTED)
set_target_properties(OpenColorIO::OpenColorIO PROPERTIES
IMPORTED_LOCATION "{dep.as_posix()}/bin/OpenColorIO_2_4.dll"
IMPORTED_IMPLIB "{dep.as_posix()}/lib/OpenColorIO.lib"
INTERFACE_INCLUDE_DIRECTORIES "{dep.as_posix()}/include")
endif()
''')
output(ocio/'OpenColorIOConfigVersion.cmake').write_bytes((dep/'lib/cmake/OpenColorIO/OpenColorIOConfigVersion.cmake').read_bytes())
args += ['-DOpenColorIO_DIR='+ocio.as_posix()]
initial = output(root/'dependencies.cmake')
initial.write_text('set(PLATFORM_BUNDLED_LIBRARIES_RELEASE "'+';'.join(p.as_posix() for p in sorted((dep/'bin').glob('*.dll')))+'" CACHE STRING "Host runtime dependencies")\n')
args += ['-C',str(initial)]
cmake_paths(args)
wrapper = output(root/'toolchain.cmd')
wrapper.write_text('@echo off\nset "TEMP='+env['TEMP']+'"\nset "TMP='+env['TMP']+'"\n'
    'call "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul\n'
    'set "TEMP='+env['TEMP']+'"\nset "TMP='+env['TMP']+'"\n%*\n')
command = [str(wrapper),'C:/Users/jun/Tools/CMake/cmake-3.31.2-windows-x86_64/bin/cmake.exe',*args]
output(root/'configure-command.json').write_text(json.dumps(command,indent=2))
with output(root/'configure.log').open('w') as log:
    result = subprocess.run(command,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT)
print('configure exit:',result.returncode)
sys.exit(result.returncode)
