using System.Text;

namespace X360Extract;

/// <summary>
/// GDFX (Game Disc Format for Xbox) filesystem reader.
///
/// The GDFX header is a 36-byte structure:
///   0x00  magic: "MICROSOFT*XBOX*MEDIA" (20 bytes)
///   0x14  rootSector (u32 LE)
///   0x18  rootSize (u32 LE)
///   0x1C  creationTime (u64 LE, Windows FILETIME)
///
/// File entries are variable-length, padded to 4-byte boundaries:
///   u32 unknown
///   u32 sector
///   u32 size
///   u8  attributes  (0x10 = directory, 0x80 = normal file)
///   u8  nameLen
///   u8  name[nameLen]
///
/// A u32 of 0xFFFFFFFF marks the end of a directory listing.
///
/// File data is at: baseOffset + sector * 2048
/// where baseOffset = gdfxHeaderOffset - 0x10000
/// </summary>
public sealed class GdfxFilesystem
{
    public long HeaderOffset;
    public long BaseOffset;
    public uint RootSector;
    public uint RootSize;

    public readonly IsoImage Iso;

    public GdfxFilesystem(IsoImage iso, long headerOffset)
    {
        Iso = iso;
        HeaderOffset = headerOffset;
        BaseOffset = headerOffset - 0x10000;

        byte[] hdr = new byte[0x24];
        iso.Read(headerOffset, hdr);

        // Verify magic
        string magic = Encoding.ASCII.GetString(hdr, 0, 0x14);
        if (magic != "MICROSOFT*XBOX*MEDIA")
            throw new InvalidDataException($"Invalid GDFX magic: '{magic}'");

        RootSector = BitConverter.ToUInt32(hdr, 0x14);
        RootSize = BitConverter.ToUInt32(hdr, 0x18);
    }

    /// <summary>Convert a GDFX sector number to an ISO byte offset.</summary>
    public long SectorToOffset(uint sector) => BaseOffset + (long)sector * 2048;

    /// <summary>
    /// Read a GDFX directory listing. The directory spans <paramref name="size"/> bytes
    /// (a whole number of 0x800 sectors). Entries are variable-length, padded to a
    /// 4-byte boundary, and grouped per sector: the tail of each sector batch is
    /// 0xFF-padded up to the sector boundary, and the next batch starts at the next
    /// 0x800 boundary. A 0xFFFFFFFF word therefore marks either the end of the whole
    /// directory or merely the end of one sector's batch.
    /// </summary>
    public List<GdfxEntry> ReadDirectory(uint sector, uint size)
    {
        var entries = new List<GdfxEntry>();
        long start = SectorToOffset(sector);
        long end = start + size;
        long pos = start;

        while (pos + 14 <= end && pos + 14 <= Iso.Length)
        {
            byte[] rec = new byte[14];
            Iso.Read(pos, rec);

            uint unknown = BitConverter.ToUInt32(rec, 0);
            if (unknown == 0xFFFFFFFF)
            {
                // Possibly the end of this sector's batch. Jump to the next 0x800
                // boundary (relative to the directory start) if it is still inside
                // the directory and the gap is pure 0xFF padding.
                long next = start + ((((pos - start) >> 11) + 1) << 11);
                if (next < end && IsZeroFill(pos, (int)(next - pos)))
                {
                    pos = next;
                    continue;
                }
                break; // true terminator
            }

            uint entrySector = BitConverter.ToUInt32(rec, 4);
            uint entrySize = BitConverter.ToUInt32(rec, 8);
            byte attrs = rec[12];
            byte nameLen = rec[13];

            if (nameLen < 1 || nameLen > 100) break;

            byte[] nameBuf = new byte[nameLen];
            Iso.Read(pos + 14, nameBuf);
            string name;
            try { name = Encoding.ASCII.GetString(nameBuf); }
            catch { break; }

            if (!IsValidName(name)) break;

            entries.Add(new GdfxEntry
            {
                Name = name,
                Sector = entrySector,
                Size = entrySize,
                Attributes = attrs,
                IsDirectory = (attrs & 0x10) != 0,
            });

            pos += (14 + nameLen + 3) & ~3;
        }

        return entries;
    }

    private static bool IsValidName(string name)
    {
        foreach (char c in name)
        {
            if (c <= 0x20 || c >= 0x7F)
                return false;
        }
        return true;
    }

    /// <summary>True if the range [start, start+count) is all 0xFF (sector-batch padding).</summary>
    private bool IsZeroFill(long start, int count)
    {
        var buf = new byte[count];
        Iso.Read(start, buf);
        foreach (byte b in buf)
            if (b != 0xFF) return false;
        return true;
    }

    /// <summary>Read file content into a callback, in chunks.</summary>
    public void ReadFile(uint sector, uint size, Action<byte[], int, int> write)
    {
        long off = SectorToOffset(sector);
        long remaining = size;
        var buf = new byte[1 << 20]; // 1 MiB chunks
        while (remaining > 0)
        {
            int n = (int)Math.Min(buf.Length, remaining);
            if (off + n > Iso.Length) n = (int)(Iso.Length - off);
            if (n <= 0) break;
            Iso.Read(off, buf, 0, n);
            write(buf, 0, n);
            off += n;
            remaining -= n;
        }
    }

}

public struct GdfxEntry
{
    public string Name;
    public uint Sector;
    public uint Size;
    public byte Attributes;
    public bool IsDirectory;
}
