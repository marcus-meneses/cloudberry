#!/usr/bin/env python3
"""Resolve every pinned audited core external against target-only SDK archives."""
import hashlib,json,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[2];work=root/'bin/tests/sdk-link';work.mkdir(parents=True,exist_ok=True)
sdk=root/'bin/sdk';prefix=root/'.toolchain/cross/bin/x86_64-elf-';cc=str(prefix)+'gcc'
symbols=[line for line in (root/'src/sdk/required-symbols.txt').read_text().splitlines() if line and not line.startswith('#')]
assembly=work/'required.s';assembly.write_text('.section .rodata\n.global cb_runtime_required\ncb_runtime_required:\n'+''.join('.quad '+symbol+'\n' for symbol in symbols)+'.section .note.GNU-stack,"",@progbits\n')
obj=work/'required.o';subprocess.run([cc,'-c',str(assembly),'-o',str(obj)],check=True)
probe=work/'main.c';probe.write_text('int main(int argc,char **argv){(void)argv;return argc<0;}\n')
cpp=work/'headers.cpp';cpp.write_text('#include "'+str(root/'src/sdk/libc.h')+'"\nextern "C" cb_status_t sdk_cpp_boundary(){uint64_t now;cb_status_t s=cb_runtime_now(cb_libc_runtime(),&now);if(s)return s;cb_handle_t h=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);return cb_system_release(h);}\n')
cpp_obj=work/'headers.o';subprocess.run([str(prefix)+'g++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-ffreestanding','-fno-pie','-mno-red-zone','-c',str(cpp),'-o',str(cpp_obj)],check=True)
binary=work/'required.elf'
command=[cc,'-ffreestanding','-fno-pie','-fno-stack-protector','-mno-red-zone','-nostdlib','-static','-no-pie',
         '-Wl,--build-id=none,-z,noexecstack,-z,max-page-size=4096,-T,'+str(root/'src/sdk/application.ld'),
         '-L'+str(sdk/'sysroot/x86_64-elf/lib'),str(sdk/'crt0.o'),str(probe),str(obj),str(cpp_obj),'-Wl,--start-group',str(sdk/'libcloudberry.a'),'-lc','-lm','-lgcc','-Wl,--end-group','-o',str(binary)]
subprocess.run(command,check=True)
undefined=subprocess.check_output([str(prefix)+'nm','-u',str(binary)],text=True).strip()
if undefined:raise RuntimeError('unresolved target externals: '+undefined)
segments=subprocess.check_output([str(prefix)+'readelf','-l',str(binary)],text=True)
if any(name in segments for name in ('INTERP','DYNAMIC','TLS')):raise RuntimeError('unsupported executable segment')
(work/'report.json').write_text(json.dumps(dict(status='passed',symbols=symbols,command=command,sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),guest_engine=False),indent=2)+'\n')
print(f'PASS target SDK link: {len(symbols)} audited externals, no host libraries, unresolved symbols, dynamic loader or TLS')
