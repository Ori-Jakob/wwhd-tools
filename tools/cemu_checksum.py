"""Cemu graphic-pack moduleMatches checksum of an rpx, as RPLLoader_BeginCemuhookCRC computes it."""
import struct
import sys
import zlib

SHF_RPL_ZLIB = 0x08000000
SHT_NOBITS = 8
SHT_SYMTAB = 2
SHT_STRTAB = 3


def le32(v):
    return struct.pack('<I', v & 0xFFFFFFFF)


def cemu_crc(path):
    data = open(path, 'rb').read()
    (e_shoff,) = struct.unpack('>I', data[0x20:0x24])
    e_shentsize, e_shnum, e_shstrndx = struct.unpack('>HHH', data[0x2E:0x34])
    secs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        secs.append(struct.unpack('>IIIIIIIIII', data[off:off + 40]))
    sym = -1
    strt = -1
    for i, s in enumerate(secs):
        if s[1] == SHT_SYMTAB and sym == -1:
            sym = i
        if s[1] == SHT_STRTAB and i != e_shstrndx and strt == -1:
            strt = i
    c = zlib.crc32(b'\x7fRPX', 0)
    for v in (e_shnum, sym, strt, e_shstrndx):
        c = zlib.crc32(le32(v), c)
    for s in secs:
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, _, _, sh_addralign, _ = s
        raw = None
        if sh_type == SHT_NOBITS:
            raw_size = sh_size
        elif sh_flags & SHF_RPL_ZLIB:
            (raw_size,) = struct.unpack('>I', data[sh_offset:sh_offset + 4])
            raw = zlib.decompress(data[sh_offset + 4:sh_offset + sh_size])
        else:
            raw_size = sh_size
            raw = data[sh_offset:sh_offset + sh_size]
        for v in (sh_name, sh_type, sh_flags, sh_addr, raw_size, sh_addralign):
            c = zlib.crc32(le32(v), c)
        if raw is not None and raw_size > 0:
            c = zlib.crc32(raw, c)
    return c & 0xFFFFFFFF


if __name__ == '__main__':
    for path in sys.argv[1:]:
        print(f'0x{cemu_crc(path):08x}  {path}')
