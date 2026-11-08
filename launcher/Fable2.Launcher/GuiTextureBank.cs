using System.Buffers.Binary;
using System.IO;
using System.IO.Compression;
using System.Text;

namespace Fable2Launcher;

// The compact GUI banks have named, uncompressed payloads and a zlib-wrapped
// directory. Other Fable banks have a different entry layout.
internal sealed class GuiTextureBank
{
    internal readonly record struct Entry(int TableOffset, int Offset, int Length);

    private const int HeaderLength = 17;
    private readonly byte[] _bytes;
    private readonly byte[] _table;
    private readonly int _baseOffset;
    private readonly MemoryStream _extra = new();
    internal Dictionary<string, Entry> Entries { get; } = new(StringComparer.OrdinalIgnoreCase);

    internal GuiTextureBank(byte[] bytes)
    {
        _bytes = bytes;
        if (bytes.Length < HeaderLength || ReadInt(bytes, 4) != 3 || bytes[8] != 0)
            throw new InvalidDataException("Unknown GUI texture bank header.");

        _baseOffset = ReadInt(bytes, 0);
        int packedLength = ReadInt(bytes, 9);
        int tableLength = ReadInt(bytes, 13);
        if (_baseOffset < HeaderLength || _baseOffset > bytes.Length ||
            packedLength < 2 || packedLength > _baseOffset - HeaderLength ||
            tableLength < 4 || tableLength > 2 * 1024 * 1024)
            throw new InvalidDataException("Invalid GUI texture directory size.");

        // The disc's zlib wrapper omits the Adler-32 trailer. The DEFLATE
        // stream still has a normal end marker.
        int wrapper = BinaryPrimitives.ReadUInt16BigEndian(bytes.AsSpan(HeaderLength));
        if (wrapper >> 8 != 0x78 || wrapper % 31 != 0 || (wrapper & 0x20) != 0)
            throw new InvalidDataException("Unknown GUI texture directory compression.");

        _table = new byte[tableLength];
        using (var packed = new MemoryStream(bytes, HeaderLength + 2, packedLength - 2))
        using (var inflater = new DeflateStream(packed, CompressionMode.Decompress))
        {
            inflater.ReadExactly(_table);
            if (inflater.ReadByte() != -1)
                throw new InvalidDataException("GUI texture directory exceeds its declared size.");
        }

        int count = ReadInt(_table, 0);
        int cursor = 4;
        if (count < 0 || count > (_table.Length - cursor) / 14)
            throw new InvalidDataException("Invalid GUI texture directory entry count.");
        for (int i = 0; i < count; i++)
        {
            if (cursor > _table.Length - 4)
                throw new InvalidDataException("Truncated GUI texture directory.");
            int nameLength = ReadInt(_table, cursor);
            cursor += 4;
            if (nameLength < 2 || nameLength > _table.Length - cursor - 8 ||
                _table[cursor + nameLength - 1] != 0)
                throw new InvalidDataException("Invalid GUI texture name.");
            string name = Encoding.UTF8.GetString(_table, cursor, nameLength - 1);
            cursor += nameLength;
            int relativeOffset = ReadInt(_table, cursor);
            int length = ReadInt(_table, cursor + 4);
            if (relativeOffset < 0 || length < 0 || relativeOffset > bytes.Length - _baseOffset - length ||
                !Entries.TryAdd(name, new Entry(cursor, _baseOffset + relativeOffset, length)))
                throw new InvalidDataException("Invalid GUI texture entry.");
            cursor += 8;
        }
        if (cursor != _table.Length)
            throw new InvalidDataException("Unexpected data in GUI texture directory.");
    }

    internal Span<byte> Payload(Entry entry) => _bytes.AsSpan(entry.Offset, entry.Length);

    internal void Replace(Entry entry, ReadOnlySpan<byte> payload)
    {
        // A bank can alias payload offsets across entries. Append a new
        // payload and redirect only this entry, even when the size matches.
        while (((_bytes.Length + _extra.Length) & 7) != 0) _extra.WriteByte(0);
        int offset = checked(_bytes.Length + (int)_extra.Length - _baseOffset);
        WriteInt(_table, entry.TableOffset, offset);
        WriteInt(_table, entry.TableOffset + 4, payload.Length);
        _extra.Write(payload);
    }

    internal void Write(string path)
    {
        if (_extra.Length != 0)
        {
            using var packed = new MemoryStream();
            using (var compressor = new ZLibStream(packed, CompressionLevel.Optimal, leaveOpen: true))
                compressor.Write(_table);
            if (packed.Length > _baseOffset - HeaderLength)
                throw new InvalidDataException("The new GUI texture directory does not fit.");
            var directory = _bytes.AsSpan(HeaderLength, _baseOffset - HeaderLength);
            directory.Clear();
            packed.GetBuffer().AsSpan(0, (int)packed.Length).CopyTo(directory);
            WriteInt(_bytes, 9, (int)packed.Length);
        }
        using var file = File.Create(path);
        file.Write(_bytes);
        _extra.WriteTo(file);
    }

    internal static int ReadInt(ReadOnlySpan<byte> bytes, int offset) =>
        BinaryPrimitives.ReadInt32BigEndian(bytes[offset..]);

    internal static void WriteInt(Span<byte> bytes, int offset, int value) =>
        BinaryPrimitives.WriteInt32BigEndian(bytes[offset..], value);
}
