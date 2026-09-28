using System.IO;
using System.IO.Compression;

namespace RandoPack;

/// <summary>
/// Cemu's graphic-pack <c>moduleMatches</c> checksum of an rpx, as
/// <c>RPLLoader_BeginCemuhookCRC</c> in Cemu's rpl.cpp computes it. This is
/// the C# twin of tools/cemu_checksum.py; keep the two in step.
///
/// The CRC32 is seeded with <c>7F 'R' 'P' 'X'</c>, then hashes the section
/// count and the symtab, strtab and shstrtab indices as little-endian words.
/// For every section header it hashes name offset, type, flags, address,
/// decompressed size and alignment the same way, followed by the decompressed
/// section contents.
/// </summary>
public static class CemuRplChecksum
{
    private const uint ShfRplZlib = 0x08000000;
    private const uint ShtSymtab = 2;
    private const uint ShtStrtab = 3;
    private const uint ShtNobits = 8;

    public static uint Compute(string path) => Compute(File.ReadAllBytes(path));

    public static uint Compute(byte[] data)
    {
        if (data.Length < 0x34 || data[0] != 0x7F || data[1] != (byte)'E' || data[2] != (byte)'L' || data[3] != (byte)'F')
            throw new InvalidDataException("not an ELF/RPX file");

        uint shoff = Be32(data, 0x20);
        int shentsize = Be16(data, 0x2E);
        int shnum = Be16(data, 0x30);
        int shstrndx = Be16(data, 0x32);

        var secs = new uint[shnum][];
        for (int i = 0; i < shnum; i++)
        {
            long off = shoff + (long)i * shentsize;
            if (off < 0 || off + 40 > data.Length)
                throw new InvalidDataException("section header table out of range");
            var s = new uint[10];
            for (int j = 0; j < 10; j++)
                s[j] = Be32(data, (int)off + j * 4);
            secs[i] = s;
        }

        // Cemu takes the first symtab and the first strtab that is not the
        // section-name table; -1 hashes as 0xFFFFFFFF when there is none.
        int sym = -1, str = -1;
        for (int i = 0; i < shnum; i++)
        {
            if (secs[i][1] == ShtSymtab && sym == -1) sym = i;
            if (secs[i][1] == ShtStrtab && i != shstrndx && str == -1) str = i;
        }

        uint c = Crc32.Update(0, new byte[] { 0x7F, (byte)'R', (byte)'P', (byte)'X' });
        c = Crc32.Update(c, Le32((uint)shnum));
        c = Crc32.Update(c, Le32(unchecked((uint)sym)));
        c = Crc32.Update(c, Le32(unchecked((uint)str)));
        c = Crc32.Update(c, Le32((uint)shstrndx));

        foreach (var s in secs)
        {
            uint name = s[0], type = s[1], flags = s[2], addr = s[3], offset = s[4], size = s[5], align = s[8];
            byte[]? raw = null;
            int rawStart = 0, rawLen = 0;
            uint rawSize;

            if (type == ShtNobits)
            {
                rawSize = size;
            }
            else
            {
                if ((long)offset + size > data.Length)
                    throw new InvalidDataException("section contents out of range");
                if ((flags & ShfRplZlib) != 0)
                {
                    rawSize = Be32(data, (int)offset);
                    raw = Inflate(data, (int)offset + 4, (int)size - 4);
                    rawStart = 0;
                    rawLen = raw.Length;
                }
                else
                {
                    rawSize = size;
                    raw = data;
                    rawStart = (int)offset;
                    rawLen = (int)size;
                }
            }

            c = Crc32.Update(c, Le32(name));
            c = Crc32.Update(c, Le32(type));
            c = Crc32.Update(c, Le32(flags));
            c = Crc32.Update(c, Le32(addr));
            c = Crc32.Update(c, Le32(rawSize));
            c = Crc32.Update(c, Le32(align));
            if (raw != null && rawSize > 0)
                c = Crc32.Update(c, raw.AsSpan(rawStart, rawLen));
        }
        return c;
    }

    private static byte[] Inflate(byte[] data, int start, int length)
    {
        using var input = new MemoryStream(data, start, length, writable: false);
        using var z = new ZLibStream(input, CompressionMode.Decompress);
        using var output = new MemoryStream();
        z.CopyTo(output);
        return output.ToArray();
    }

    private static uint Be32(byte[] d, int o) =>
        (uint)d[o] << 24 | (uint)d[o + 1] << 16 | (uint)d[o + 2] << 8 | d[o + 3];

    private static int Be16(byte[] d, int o) => d[o] << 8 | d[o + 1];

    private static byte[] Le32(uint v) =>
        new[] { (byte)v, (byte)(v >> 8), (byte)(v >> 16), (byte)(v >> 24) };
}

/// <summary>zlib-compatible CRC-32 (reflected 0xEDB88320); Update(0, ...) equals zlib.crc32.</summary>
public static class Crc32
{
    private static readonly uint[] Table = BuildTable();

    private static uint[] BuildTable()
    {
        var t = new uint[256];
        for (uint i = 0; i < 256; i++)
        {
            uint r = i;
            for (int k = 0; k < 8; k++)
                r = (r & 1) != 0 ? 0xEDB88320 ^ (r >> 1) : r >> 1;
            t[i] = r;
        }
        return t;
    }

    public static uint Update(uint crc, ReadOnlySpan<byte> data)
    {
        uint r = ~crc;
        foreach (byte b in data)
            r = Table[(r ^ b) & 0xFF] ^ (r >> 8);
        return ~r;
    }
}
