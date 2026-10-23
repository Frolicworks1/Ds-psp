"""Independent ARM routines test init ordering and state persistence, without donor data."""
import pathlib
import struct
import subprocess
import sys
import tempfile


def fixture(second_expected=2, array_size=16, array=0x1180, init=0x1200):
    data = bytearray(2048)
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    struct.pack_into('<16sHHIIIIIHHHHHH', data, 0, ident, 3, 40, 1, 0, 52, 0, 0, 52, 32, 2, 0, 0, 0)
    struct.pack_into('<8I', data, 52, 1, 0, 0x1000, 0, len(data), len(data), 7, 4096)
    struct.pack_into('<8I', data, 84, 2, 0x100, 0x1100, 0, 32, 32, 6, 4)
    for i, pair in enumerate([(12, init), (25, array), (27, array_size), (0, 0)]):
        struct.pack_into('<II', data, 0x100 + i * 8, *pair)
    struct.pack_into('<4I', data, 0x180, 0x1240, 0, 0xffffffff, 0x1280)
    for off, expected, value in [(0x200, 0, 1), (0x240, 1, 2), (0x280, second_expected, 3)]:
        # Load shared counter; check predecessor; update; return. SVC on wrong order.
        code = [0xe59f101c, 0xe5910000, 0xe3500000 | expected, 0x1a000003,
                0xe3a00000 | value, 0xe5810000, 0xe12fff1e, 0xe1a00000,
                0xef000000, 0x1700]
        struct.pack_into('<10I', data, off, *code)
    return data


with tempfile.TemporaryDirectory(prefix='ds-init-') as tmp:
    path = pathlib.Path(tmp) / 'fixture.so'

    def run(data, limit='7', mode='constructors'):
        path.write_bytes(data)
        result = subprocess.run([sys.argv[1], str(path), mode, limit], capture_output=True, text=True)
        assert 'AddressSanitizer' not in result.stderr and 'runtime error:' not in result.stderr, result.stderr
        return result

    r = run(fixture())
    assert r.returncode == 0 and 'total=4 attempted=2 returned=2 skipped=2 pending=0' in r.stdout, r.stdout + r.stderr
    assert r.stdout.index('DT_INIT entry=') < r.stdout.index('CONSTRUCTOR index=0') < r.stdout.index('CONSTRUCTOR index=3')
    r = run(fixture(second_expected=9))
    assert r.returncode == 2 and 'returned=1' in r.stdout and 'opcode=ef000000' in r.stdout, r.stdout
    r = run(fixture(), '6')
    assert r.returncode == 2 and 'attempted=0' in r.stdout and 'limit reached' in r.stdout, r.stdout
    for args in [dict(array_size=15), dict(array=0x17fc), dict(init=0x1800)]:
        r = run(fixture(**args))
        assert r.returncode == 1 and 'loader error=-11' in r.stderr, r.stdout + r.stderr
    data = fixture()
    struct.pack_into('<I', data, 0x180, 0x90000000)
    r = run(data)
    assert r.returncode == 2 and 'target outside image' in r.stdout and 'pending=3' in r.stdout, r.stdout
    data = fixture()
    struct.pack_into('<II', data, 0x118, 1, 0)  # No DT_NULL terminator.
    r = run(data)
    assert r.returncode == 1 and 'loader error=-11' in r.stderr, r.stderr
    # Public exports and a state-dependent lifecycle: missing/failed stages must
    # stop the runner, and JNI_OnLoad's return version must be checked.
    data = fixture() + bytearray(2048)
    struct.pack_into('<II', data, 52+16, len(data), len(data))
    struct.pack_into('<I', data, 32, 0x800)  # section table
    struct.pack_into('<HH', data, 46, 40, 4)
    names=['JNI_OnLoad', 'Java_com_ea_blast_MainActivity_NativeOnCreate',
           'Java_com_ea_blast_AndroidRenderer_NativeOnSurfaceCreated',
           'Java_com_ea_blast_AndroidRenderer_NativeOnSurfaceChanged',
           'Java_com_ea_blast_KeyboardAndroid_NativeOnVisibilityChanged',
           'Java_com_ea_blast_MainActivity_NativeOnWindowFocusChanged',
           'Java_com_ea_blast_AndroidRenderer_NativeOnDrawFrame']
    strings=bytearray(b'\0')
    for i,name in enumerate(names):
        offset=0xc00+i*0x40
        if not i:
            code=[0xe3a00801,0xe2800004,0xe12fff1e] # JNI_VERSION_1_4
        else:
            code=[0xe59f1018,0xe5910000,0xe3500000|(i+2),0x1f000000,
                  0xe2800001,0xe5810000,0xe12fff1e,0xe1a00000,0x1700]
        struct.pack_into('<'+'I'*len(code),data,offset,*code)
        struct.pack_into('<IIIBBH',data,0x900+(i+1)*16,len(strings),0x1000+offset,len(code)*4,0x12,0,3)
        strings.extend(name.encode()+b'\0')
    data[0xa00:0xa00+len(strings)]=strings
    struct.pack_into('<10I',data,0x800+40,0,11,0,0,0x900,8*16,2,0,4,16)
    struct.pack_into('<10I',data,0x800+80,0,3,0,0,0xa00,len(strings),0,0,1,0)
    r=run(data,'100','startup')
    assert r.returncode==0 and 'JNI_OnLoad result=00010004 status=0' in r.stdout and 'stage=4 status=0' in r.stdout, r.stdout+r.stderr
    bad=data.copy();struct.pack_into('<I',bad,0xc00,0xe3a00000) # wrong JNI version
    r=run(bad,'100','startup')
    assert r.returncode==2 and 'STARTUP NativeOnCreate' not in r.stdout, r.stdout
    bad=data.copy();struct.pack_into('<I',bad,0xc80,0xef000000) # surface-create failure
    r=run(bad,'100','startup')
    assert r.returncode==2 and 'NativeOnSurfaceChanged entry=' not in r.stdout, r.stdout
print('initializer metadata, DT_INIT order, shared state, skips, bounds and fail-fast checks passed')
