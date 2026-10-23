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

    def run(data, limit='7'):
        path.write_bytes(data)
        result = subprocess.run([sys.argv[1], str(path), 'constructors', limit], capture_output=True, text=True)
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
print('initializer metadata, DT_INIT order, shared state, skips, bounds and fail-fast checks passed')
