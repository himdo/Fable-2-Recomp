using System.Globalization;
using System.Text;

namespace X360Extract;

public enum IsoEntryKind
{
    VolumeTag,
    Parent,
    File,
    Directory,
}

public readonly record struct IsoEntry(IsoEntryKind Kind, string Name, ulong Sector, ulong Size)
{
    public bool IsFile => Kind == IsoEntryKind.File;
    public bool IsDirectory => Kind == IsoEntryKind.Directory;
}

/// <summary>
/// Minimal ISO 9660 (primary volume descriptor) reader. Sufficient for Xbox 360
/// game images, which are plain 2048-byte-sector ISO 9660 with upper-case level-1 names.
/// </summary>
public sealed class IsoImage : IDisposable
{
    public const int SectorSize = 2048;

    private readonly FileStream _fs;
    private readonly byte[] _sector = new byte[SectorSize];
    public long ImageLength { get; }
    public long Length => ImageLength;

    public IsoImage(string path)
    {
        _fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        ImageLength = _fs.Length;
    }

    /// <summary>Read an absolute byte range (used by the STFS scanner).</summary>
    public void Read(long offset, byte[] buf)
        => Read(offset, buf, 0, buf.Length);

    public void Read(long offset, byte[] buf, int bufOffset, int count)
    {
        _fs.Seek(offset, SeekOrigin.Begin);
        int off = 0;
        while (off < buf.Length)
        {
            int n = _fs.Read(buf, off, buf.Length - off);
            if (n <= 0) { Array.Clear(buf, off, buf.Length - off); break; }
            off += n;
        }
    }



    /// <summary>Read one full 2048-byte sector.</summary>
    public void ReadSector(ulong lba)
    {
        _fs.Seek((long)lba * SectorSize, SeekOrigin.Begin);
        int off = 0;
        while (off < SectorSize)
        {
            int n = _fs.Read(_sector, off, SectorSize - off);
            if (n <= 0)
            {
                Array.Clear(_sector, off, SectorSize - off);
                break;
            }
            off += n;
        }
    }

    /// <summary>Enumerate entries of the root directory from the primary volume descriptor.</summary>
    public List<IsoEntry> ListRoot()
    {
        // Primary volume descriptor: usually sector 16; scan the descriptor set to be safe.
        byte[] pvd = null!;
        for (ulong lba = 16; lba < 16 + 256 && lba * (ulong)SectorSize < (ulong)ImageLength; lba++)
        {
            ReadSector(lba);
            byte type = _sector[0];
            if (type == 1 && Encoding.ASCII.GetString(_sector, 1, 5) == "CD001")
            {
                pvd = (byte[])_sector.Clone();
                break;
            }
            if (type == 255) // terminator
                break;
        }
        if (pvd == null)
            throw new InvalidDataException("no ISO 9660 primary volume descriptor found - not a valid ISO 9660 image?");

        uint rootLba = BitConverter.ToUInt32(pvd, 156 + 2);
        uint rootSize = (uint)Read32Var(pvd, 156 + 10, isVarInt: true);

        var entries = new List<IsoEntry>();
        foreach (var rec in ReadDirectory(rootLba, rootSize))
            entries.Add(rec);
        return entries;
    }

    /// <summary>Enumerate the entries of one directory (files and sub-directories).</summary>
    public IEnumerable<IsoEntry> ReadDirectory(ulong lba, ulong size)
    {
        var results = new List<IsoEntry>();
        if (size == 0) return results;

        ulong pos = lba;
        ulong end = lba + (size + SectorSize - 1) / SectorSize;
        while (pos < end)
        {
            ReadSector(pos);
            int off = 0;
            while (off < SectorSize)
            {
                int recLen = _sector[off];
                if (recLen == 0)
                {
                    off = (off + 1) & ~1;
                    continue;
                }
                if (off + recLen > SectorSize)
                {
                    // record runs into next sector; copy the tail over
                    byte[] big = new byte[recLen];
                    Array.Copy(_sector, off, big, 0, SectorSize - off);
                    off = SectorSize;
                    for (int s = 1; off < recLen; s++)
                    {
                        ReadSector(pos + (ulong)s);
                        Array.Copy(_sector, 0, big, SectorSize * s, Math.Min(recLen - SectorSize * s, SectorSize));
                    }
                    results.Add(ParseRecord(big));
                    continue;
                }
                byte[] small = new byte[recLen];
                Array.Copy(_sector, off, small, 0, recLen);
                off += recLen;
                results.Add(ParseRecord(small));
            }
            pos++;
        }
        return results;
    }

    private static IsoEntry ParseRecord(byte[] r)
    {
        int len = r[0];
        if (len < 33)
            throw new InvalidDataException($"bad ISO directory record length {len}");
        ulong lba = BitConverter.ToUInt32(r, 2);
        ulong size = Read32Var(r, 10);
        byte flags = r[25];

        int nameLen = r[32];
        string name = nameLen == 0 ? "" : Encoding.ASCII.GetString(r, 33, nameLen).TrimEnd('\0');
        name = name.Replace('_', ' '); // ISO9660 escape for spaces

        // strip the ";1" version suffix
        int semi = name.LastIndexOf(';');
        if (semi >= 0)
            name = name[..semi];

        if (nameLen == 0)
            return new IsoEntry(IsoEntryKind.VolumeTag, name, lba, size);
        if (nameLen == 1)
            return new IsoEntry(name[0] == '.' ? IsoEntryKind.Parent : IsoEntryKind.VolumeTag, name, lba, size);

        IsoEntryKind kind = (flags & 0x02) != 0 ? IsoEntryKind.Directory : IsoEntryKind.File;
        return new IsoEntry(kind, name, lba, size);
    }

    /// <summary>
    /// Read a file (or directory payload) in 1 MiB chunks into a callback, so arbitrarily
    /// large files can be streamed without copying them all into memory.
    /// </summary>
    public void StreamRange(ulong lba, ulong size, Action<long> onProgress, Action<byte[], int, int> processChunk)
    {
        const int chunk = 1 << 20;
        byte[] buf = new byte[chunk];
        ulong remaining = size;
        ulong pos = lba;
        long written = 0;
        while (remaining > 0)
        {
            int want = (int)Math.Min((ulong)chunk, remaining);
            _fs.Seek((long)pos * SectorSize, SeekOrigin.Begin);
            int off = 0;
            while (off < want)
            {
                int n = _fs.Read(buf, off, want - off);
                if (n <= 0)
                    throw new EndOfStreamException("short read from ISO");
                off += n;
            }
            processChunk(buf, 0, want);
            written += want;
            remaining -= (ulong)want;
            pos += (ulong)(want + SectorSize - 1) / SectorSize;
            onProgress(written);
        }
    }

    public void Dispose() => _fs.Dispose();

    private static uint Read32Var(byte[] b, int off, bool isVarInt = false)
    {
        // ISO 9660 "32-bit MSB first" or variable-length integer
        uint v = 0;
        for (int i = off; i < Math.Min(off + 8, b.Length); i += 2)
        {
            int hi = b[i];
            int lo = b[i + 1];
            if (hi == 0 && lo == 0) break;
            v = (v << 8) | (byte)hi | (byte)lo;
        }
        return v;
    }
}
