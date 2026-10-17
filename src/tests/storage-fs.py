#!/usr/bin/env python3
"""Disposable GPT/ext4 fixtures; no mounts, loops, candidate or persistent disks."""
import pathlib, subprocess, sys, tempfile, uuid
root=pathlib.Path(__file__).resolve().parents[2]
build=root/'bin/tests/isolated/tests';build.mkdir(parents=True,exist_ok=True)
exe=build/'src-storage-fs'
with tempfile.TemporaryDirectory(prefix='ext4-',dir=build) as temporary:
    work=pathlib.Path(temporary);source=work/'root';drivers=source/'boot/system/drivers';drivers.mkdir(parents=True)
    (drivers/'probe.bin').write_bytes(bytes((i*37+11)%256 for i in range(16384)))
    with (drivers/'fragment.bin').open('wb') as f:
        for i in range(80):
            f.seek(i*8192);f.write(bytes([i+1])*4096)
        f.truncate(80*8192)
    guid=uuid.UUID('9beaeafd-7835-446a-8258-00331878d4f3')
    features=('none,has_journal,ext_attr,resize_inode,dir_index,filetype,extent,64bit,'
              'flex_bg,sparse_super,large_file,huge_file,dir_nlink,extra_isize,metadata_csum')
    for block,seed,inode in ((1024,False,256),(2048,False,256),(4096,False,256),(4096,True,512)):
        extra=['-O',features+(',metadata_csum_seed' if seed else '')]
        fs=work/'filesystem';disk=work/'disk'
        with fs.open('wb') as f:f.truncate(32*1024**2)
        subprocess.run(['mke2fs','-q','-t','ext4','-F','-b',str(block),'-I',str(inode),'-E','lazy_itable_init=0,lazy_journal_init=0','-d',str(source)]+extra+[str(fs)],check=True)
        with disk.open('wb') as f:f.truncate(40*1024**2)
        subprocess.run(['sgdisk','--clear','--new=1:2048:67583','--typecode=1:8300',f'--partition-guid=1:{guid}',str(disk)],check=True,stdout=subprocess.DEVNULL)
        with disk.open('r+b') as out,fs.open('rb') as data:
            out.seek(1024**2)
            while chunk:=data.read(1024**2):out.write(chunk)
        subprocess.run([str(exe),str(disk),guid.bytes_le.hex(),'/boot/system/drivers'],check=True)
print('PASS ext4 block geometries: 1/2/4 KiB, generated independently by mke2fs')
