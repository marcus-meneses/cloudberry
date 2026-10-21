import ctypes,hashlib,pathlib,sys
lib=ctypes.CDLL(str(pathlib.Path(sys.argv[1]).resolve()))
lib.id_password_derive.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p]
lib.id_password_valid.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
for password in (b'x',b'password',b'x'*64,('é'*64).encode()):
 for rounds in (1,2,4096,600000):
  salt=bytes(range(32));out=ctypes.create_string_buffer(32)
  assert lib.id_password_derive(password,len(password),salt,rounds,out)==1
  assert out.raw==hashlib.pbkdf2_hmac('sha256',password,salt,rounds,32)
for value,good in ((b'',False),(b'x',True),(b'x'*64,True),(b'x'*65,False),(('𐀀'*64).encode(),True),(b'\xed\xa0\x80',False),(b'\xc0\x80',False),(b'\x00',False)):
 assert bool(lib.id_password_valid(value,len(value)))==good
print('PASS password KDF: hashlib cross-check, 600000 rounds, long keys and UTF-8 1..64-character boundaries')
