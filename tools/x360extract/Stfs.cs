using System.Text;

namespace X360Extract;

/// <summary>
/// Xbox 360 STFS (Standard Transaction File System) container: LIVE / PIRS / CON.
/// Format reference: Free60 wiki / Velocity StfsPackage implementation.
///
/// Layout:
///   0x000  magic: "LIVE", "PIRS" or "CON "
///   0x340  header size (u32 BE); first hash table address = (size + 0xFFF) & 0xFFFFF000
///   0x379  volume descriptor: [size u8][reserved u8][blockSeperation u8]
///          blockSeperation 1 = "female" (shift 0), 0 = "male" (shift 1)
///   0x37C  file table block count (u16 LE)
///   0x37E  file table first block number (u24 LE)
///   0x40E  display name (UTF-16 LE, 32 bytes)
///
/// File table: 64 entries of 0x40 bytes per 4 KiB block, chained through the
/// level-0 hash table.
///   entry: [name ascii 40][0x28: nameLen&0x3F | 0x80 dir | 0x40 consecutive]
///          [0x29 u24LE numBlocks][0x2F u24LE firstBlock][0x32 u16 pathIndicator
///          (0xFFFF = root)][0x34 u32BE fileSize]
/// </summary>
public sealed class StfsContainer
{
    public string Magic = "";
    public long Start;                  // offset of the container in the ISO
    public ulong HeaderSize;
    public ulong FirstHashTableAddress;
    public int Shift;                   // 0 = female, 1 = male
    public string DisplayName = "";
    public List<StfsEntry> Entries = new();

    private IsoImage _iso = null!;
    private byte[] _block = new byte[4096];

    public static readonly byte[][] Magics =
    {
        "PIRS"u8.ToArray(), "LIVE"u8.ToArray(), "CON "u8.ToArray(), "GAMC"u8.ToArray(),
    };

    public static bool TryOpen(IsoImage iso, long start, out StfsContainer container)
    {
        container = null!;
        if (start < 0 || start + 0x400 > iso.Length) return false;
        byte[] h = new byte[0x400];
        try { iso.Read(start, h); } catch { return false; }

        bool knownMagic = false;
        foreach (var m in Magics)
        {
            if (h.AsSpan(0, 4).SequenceEqual(m)) { knownMagic = true; break; }
        }
        if (!knownMagic) return false;

        ulong headerSize = Be32(h, 0x340);
        int blockSep = h[0x37B];
        ushort ftCount = (ushort)(h[0x37C] | h[0x37D] << 8);
        int ftBlock = Le24(h, 0x37E);

        // sanity
        if (headerSize < 0x400 || headerSize > 0x2000000) return false;
        if (blockSep != 0 && blockSep != 1) return false;
        if (ftCount == 0) return false;

        var c = new StfsContainer
        {
            _iso = iso,
            Start = start,
            HeaderSize = headerSize,
            FirstHashTableAddress = (headerSize + 0xFFF) & 0xFFFFF000,
            Shift = (blockSep & 1) == 1 ? 0 : 1,
            Magic = Encoding.ASCII.GetString(h, 0, 4).TrimEnd(),
        };

        // display name: UTF-16 LE at 0x40E (360 XDK layout); some tools use BE at 0x411.
        string name = ReadUtf16(h, 0x40E, 16, false);
        if (!IsPrintable(name))
            name = ReadUtf16(h, 0x411, 64, true);
        c.DisplayName = IsPrintable(name) ? name : "";

        if (!c.TryReadFileTable(ftBlock, ftCount))
            return false;

        container = c;
        return true;
    }

    private static bool IsPrintable(string s)
    {
        if (s.Length == 0) return true;
        foreach (char ch in s)
            if (ch < 0x20 || ch > 0x7E) return false;
        return true;
    }

    private static string ReadUtf16(byte[] b, int off, int chars, bool bigEndian)
    {
        var sb = new StringBuilder();
        for (int i = 0; i < chars && off + i * 2 + 1 < b.Length; i++)
        {
            int v = bigEndian ? (b[off + i * 2] << 8) | b[off + i * 2 + 1]
                              : b[off + i * 2] | (b[off + i * 2 + 1] << 8);
            if (v == 0) break;
            sb.Append((char)v);
        }
        return sb.ToString();
    }

    public static ulong Be32(byte[] b, int o) =>
        (ulong)b[o] << 24 | (ulong)b[o + 1] << 16 | (ulong)b[o + 2] << 8 | b[o + 3];

    public static int Le24(byte[] b, int o) => b[o] | b[o + 1] << 8 | b[o + 2] << 16;

    private static int Be24(byte[] b, int o) => b[o] << 16 | b[o + 1] << 8 | b[o + 2];

    /// <summary>
    /// Compute the backing data block number for a given block number.
    /// From XboxInternals StfsPackage.cpp (Hetelek/Velocity, GPL-3.0 — algorithm only).
    /// shift = 0 for female (blockSep=1), 1 for male (blockSep=0).
    /// </summary>
    public static ulong ComputeBackingDataBlockNumber(int blockNum, int shift)
    {
        long toReturn = (((blockNum + 0xAA) / 0xAA) << shift) + blockNum;
        if (blockNum < 0xAA) return (ulong)toReturn;
        if (blockNum < 0x70E4)
            return (ulong)(toReturn + (((blockNum + 0x70E4) / 0x70E4) << shift));
        return (1UL << shift) + (ulong)(toReturn + (((blockNum + 0x70E4) / 0x70E4) << shift));
    }

    /// <summary>
    /// Compute the backing hash block number for level-0 hash of the given block.
    /// </summary>
    public static ulong ComputeLevel0HashBlockNumber(int blockNum, int shift)
    {
        if (blockNum < 0xAA) return 0;
        int step0 = shift == 0 ? 0xAB : 0xAC;
        long num = (long)(blockNum / 0xAA) * step0;
        num += ((long)(blockNum / 0x70E4) + 1) << shift;
        if (blockNum / 0x70E4 == 0) return (ulong)num;
        return (ulong)(num + (1 << shift));
    }

    public ulong BlockToAddress(int blockNum) =>
        (ComputeBackingDataBlockNumber(blockNum, Shift) << 12) + FirstHashTableAddress;

    public int GetNextBlock(int blockNum)
    {
        ulong hashAddr = (ComputeLevel0HashBlockNumber(blockNum, Shift) << 12) + FirstHashTableAddress;
        int recOff = (int)(hashAddr + (ulong)(blockNum % 0xAA) * 0x18);
        if (recOff + 0x18 > _iso.Length) return -1;
        byte[] rec = new byte[0x18];
        try { _iso.Read(Start + recOff, rec); } catch { return -1; }
        int next = Be24(rec, 5);
        if (next >= 0xFFFFFF || next == 0xFFFFFE) return -1;
        return next;
    }

    private bool TryReadFileTable(int firstBlock, int count)
    {
        int cur = firstBlock;
        for (int block = 0; block < count; block++)
        {
            if (cur < 0) break;
            long addr = Start + (long)BlockToAddress(cur);
            if (addr + 4096 > _iso.Length) break;
            try { _iso.Read(addr, _block); } catch { break; }

            bool any = false;
            for (int i = 0; i < 64; i++)
            {
                int b = i * 0x40;
                int nameLen = _block[b + 0x28] & 0x3F;
                if (nameLen == 0) continue;
                if (nameLen > 40) break;
                string name = Encoding.ASCII.GetString(_block, b, nameLen);
                if (!IsPrintable(name)) continue;
                any = true;

                Entries.Add(new StfsEntry
                {
                    Index = Entries.Count,
                    Name = name,
                    IsDirectory = (_block[b + 0x28] & 0x80) != 0,
                    IsConsecutive = (_block[b + 0x28] & 0x40) != 0,
                    NumBlocks = Le24(_block, b + 0x29),
                    FirstBlock = Le24(_block, b + 0x2F),
                    PathIndicator = (_block[b + 0x32] << 8) | _block[b + 0x33],
                    FileSize = Be32(_block, b + 0x34),
                });
            }
            if (!any && block == 0) return false;   // first block with no entries: not a container

            cur = (block + 1 < count) ? GetNextBlock(cur) : -1;
        }
        return Entries.Count > 0;
    }

    /// <summary>Full path of an entry via its path-indicator chain.</summary>
    public string GetPath(StfsEntry e)
    {
        var parts = new List<string> { e.Name };
        int pi = e.PathIndicator;
        var dirs = Entries.Where(x => x.IsDirectory).ToDictionary(x => x.Index);
        // pathIndicator references the entry index of the parent directory
        while (pi != 0xFFFF && pi != -1 && dirs.TryGetValue(pi, out var d))
        {
            parts.Insert(0, d.Name);
            pi = d.PathIndicator;
        }
        return string.Join('/', parts);
    }

    /// <summary>
    /// Stream the raw (uncompressed) file data of an entry in 4 KiB block units.
    /// Walks the block chain via the hash table for non-consecutive files,
    /// or reads sequentially for consecutive files.
    /// </summary>
    public void StreamFile(StfsEntry e, Action<byte[], int, int> write)
    {
        long remaining = (long)e.FileSize;
        int cur = e.FirstBlock;
        int blocks = 0;
        var buf = new byte[4096];
        while (remaining > 0 && blocks < e.NumBlocks && cur >= 0)
        {
            long addr = Start + (long)BlockToAddress(cur);
            if (addr + 4096 > _iso.Length + Start) break;
            int n = (int)Math.Min(4096, remaining);
            _iso.Read(addr, buf);
            write(buf, 0, n);
            remaining -= n;
            blocks++;
            if (remaining > 0)
                cur = e.IsConsecutive ? cur + 1 : GetNextBlock(cur);
        }
    }
}

public sealed class StfsEntry
{
    public int Index;
    public string Name = "";
    public bool IsDirectory;
    public bool IsConsecutive;
    public int NumBlocks;
    public int FirstBlock;
    public int PathIndicator;
    public ulong FileSize;
}
