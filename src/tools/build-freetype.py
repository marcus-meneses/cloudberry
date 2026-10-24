#!/usr/bin/env python3
"""Build verified FreeType TrueType/SFNT rasterization, without file/OS shims."""
import fcntl,hashlib,json,pathlib,subprocess,tarfile,urllib.request,shutil
ROOT=pathlib.Path(__file__).resolve().parents[2]
VERSION='2.15.0'
SHA256='93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5'
UNITS=('base/ftbase','base/ftinit','base/ftdebug','base/ftbbox','base/ftbitmap','base/ftglyph','base/ftmm','base/ftsynth','truetype/truetype','sfnt/sfnt','smooth/smooth','raster/raster','autofit/autofit','psnames/psnames')
def main():
    work=ROOT/'bin/sdk/freetype';work.mkdir(parents=True,exist_ok=True)
    with (work/'build.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX);archive=work/f'freetype-{VERSION}.tar.xz'
        if not archive.exists():
            with urllib.request.urlopen(f'https://download.savannah.gnu.org/releases/freetype/freetype-{VERSION}.tar.xz',timeout=60) as r:data=r.read(12*1024*1024+1)
            if len(data)>12*1024*1024 or hashlib.sha256(data).hexdigest()!=SHA256:raise RuntimeError('FreeType download verification failed')
            archive.write_bytes(data)
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:raise RuntimeError('FreeType archive hash mismatch')
        source=work/f'freetype-{VERSION}'
        if not source.exists():
            with tarfile.open(archive) as tar:tar.extractall(work,filter='data')
        config=work/'config';config.mkdir(exist_ok=True)
        modules='''FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
'''
        options=(source/'include/freetype/config/ftoption.h').read_text()
        for name in ('FT_CONFIG_OPTION_USE_ZLIB','FT_CONFIG_OPTION_USE_BZIP2','FT_CONFIG_OPTION_USE_PNG','FT_CONFIG_OPTION_USE_HARFBUZZ','FT_CONFIG_OPTION_USE_BROTLI'):
            import re
            options=re.sub(r'^#define\s+'+name+r'\s*$', '#undef '+name,options,flags=re.M)
        (config/'ftmodule.h').write_text(modules);(config/'ftoption.h').write_text(options)
        prefix=str(ROOT/'.toolchain/cross/bin/x86_64-elf-')
        for profile,cc,ar in [('target',prefix+'gcc',prefix+'ar'),('host','gcc','ar')]:
            out=work/profile;out.mkdir(exist_ok=True)
            flags=['-std=gnu11','-O2','-g','-fno-builtin','-ffunction-sections','-fdata-sections','-DFT2_BUILD_LIBRARY',f'-DFT_CONFIG_MODULES_H="{config}/ftmodule.h"',f'-DFT_CONFIG_OPTIONS_H="{config}/ftoption.h"','-I'+str(source/'include')]
            if profile=='target':flags+=['-fno-pie','-fno-stack-protector','-mno-red-zone','-isystem',str(ROOT/'bin/sdk/sysroot/x86_64-elf/include')]
            identity=dict(version=VERSION,sha256=SHA256,flags=flags,modules=modules,options_sha256=hashlib.sha256(options.encode()).hexdigest(),compiler=subprocess.check_output([cc,'--version'],text=True).splitlines()[0],system_sha256=hashlib.sha256((ROOT/'src/handlers/graphics-vga/font-system.c').read_bytes()).hexdigest())
            stamp=out/'build.json';library=out/'libfreetype.a'
            if not stamp.exists() or json.loads(stamp.read_text())!=identity or not library.exists():
                objects=[]
                with (out/'build.log').open('w') as log:
                    for unit in UNITS:
                        obj=out/(unit.replace('/','-')+'.o');subprocess.run([cc,*flags,'-c',str(source/'src'/(unit+'.c')),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True);objects.append(str(obj))
                    obj=out/'font-system.o';subprocess.run([cc,*flags,'-c',str(ROOT/'src/handlers/graphics-vga/font-system.c'),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True);objects.append(str(obj))
                temp=out/'libfreetype.tmp.a';temp.unlink(missing_ok=True);subprocess.run([ar,'rcs',str(temp),*objects],check=True);temp.replace(library);stamp.write_text(json.dumps(identity,indent=2)+'\n')
        for name in ('LICENSE.TXT','docs/FTL.TXT'):shutil.copy2(source/name,work/pathlib.Path(name).name)
        print('FreeType: '+str(work.relative_to(ROOT))+' (verified TrueType core, owned memory faces, no host file adapter)')
if __name__=='__main__':main()
