import ctypes,importlib.util,pathlib,struct,subprocess,sys,tempfile
root=pathlib.Path(__file__).resolve().parents[1];build=pathlib.Path(sys.argv[1]).resolve();work=build/'tests/installer';spec=importlib.util.spec_from_file_location('builder',root/'tools/build-app-bundle.py');builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
lib=ctypes.CDLL(str(build/'tests/bundle-check.so'));lib.cb_bundle_check.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p];lib.cb_bundle_check.restype=ctypes.c_int
valid=(work/'sample.cbp').read_bytes();key=(build/'installer-trusted-key.bin').read_bytes()
def check(data):return lib.cb_bundle_check(ctypes.create_string_buffer(bytes(data)),len(data),ctypes.create_string_buffer(key))
def sign(data):
    with tempfile.TemporaryDirectory(dir=work) as d:
        path=pathlib.Path(d)/'message';path.write_bytes(data[:-64]);signature=subprocess.check_output(['openssl','pkeyutl','-sign','-rawin','-inkey',sys.argv[2],'-in',str(path)]);return bytes(data[:-64])+signature
assert check(valid)==0
legacy=bytearray(valid[:112]+valid[120:]);legacy[:8]=b'CBPKG001';struct.pack_into('<Q',legacy,8,len(legacy));legacy=sign(legacy)
assert check(legacy)==0 and builder.verify_bundle(legacy,key)['revision']==1
assert builder.verify_bundle(valid,key)['revision']==1
assert check(valid[:-1])!=0
bad=bytearray(valid);bad[-1]^=1;assert check(bad)!=0
for offset,value in [(8,0),(16,0),(16,208),(24,2**64-1),(32,2**64-1),(40,2**64-1),(48,8),(56,0),(112,0)]:
    bad=bytearray(valid);struct.pack_into('<Q',bad,offset,value);assert check(sign(bad))!=0,(offset,value)
bad=bytearray(valid);bad[64:80]=bytes(16);assert check(sign(bad))!=0
bad=bytearray(valid);bad[120]=ord('_');assert check(sign(bad))!=0
name,manifest,code=struct.unpack_from('<QQQ',valid,16);at=120+name+manifest+code
bad=bytearray(valid);n=struct.unpack_from('<Q',valid,at)[0];bad[at+16:at+16+n]=b'../'+b'x'*(n-3);assert check(sign(bad))!=0
bad=bytearray(valid);bad[80]^=1;assert check(sign(bad))!=0
print('PASS signed bundles: issuer/signature, truncation, extent/overflow fields, privilege flags, identity/name and resource traversal')
