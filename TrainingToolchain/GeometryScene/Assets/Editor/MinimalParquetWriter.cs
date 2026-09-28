using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

/// <summary>
/// 极简 Parquet 文件写入器（纯 C#，无外部依赖）。
///
/// 输出格式与 ImageToScene/data/generate_data.py 的约定一致：
///     image   : BINARY  —— PNG/JPEG 编码的图片字节
///     objects : BINARY + ConvertedType=UTF8 —— 场景 JSON 字符串
///
/// 实现范围：单个 Row Group、Plain 编码、UNCOMPRESSED、所有列 REQUIRED（非空）、
/// 每个列按固定行数分页（Data Page V1）。文件元数据使用 Thrift Compact Protocol 编写，
/// 可被 pyarrow.parquet / pandas / spark 等标准读取器直接读取。
/// </summary>
internal static class MinimalParquetWriter
{
    private static readonly byte[] ParquetMagic = { (byte)'P', (byte)'A', (byte)'R', (byte)'1' };

    /// <summary>每个 Data Page 包含的最大行数（防止单页过大）。</summary>
    private const int ValuesPerPage = 256;

    // ---- Parquet 枚举常量（parquet.thrift） ----
    private const short TypeBinary = 6;            // Type.BYTE_ARRAY
    private const short EncodingPlain = 0;         // Encoding.PLAIN
    private const short EncodingRle = 3;           // Encoding.RLE
    private const short RepetitionRequired = 0;    // FieldRepetitionType.REQUIRED
    private const short ConvertedTypeUtf8 = 0;     // ConvertedType.UTF8
    private const short PageTypeDataPage = 0;      // PageType.DATA_PAGE
    private const short CodecUncompressed = 0;     // CompressionCodec.UNCOMPRESSED

    /// <summary>一列的定义与数据。</summary>
    public sealed class Column
    {
        public string Name;
        /// <summary>true 表示按 UTF8 字符串列写入（ConvertedType=UTF8），false 为纯二进制。</summary>
        public bool IsUtf8;
        /// <summary>每一行的值（按行顺序）。</summary>
        public IList<byte[]> Values;
    }

    /// <summary>将各列写入 parquet 文件（各列行数必须一致）。</summary>
    public static void Write(string path, params Column[] columns)
    {
        if (columns == null || columns.Length == 0)
        {
            throw new ArgumentException("至少需要一列");
        }
        long rowCount = columns[0].Values.Count;
        foreach (Column c in columns)
        {
            if (c.Values.Count != rowCount)
            {
                throw new ArgumentException("各列行数不一致: " + c.Name);
            }
        }

        using (FileStream fs = new FileStream(path, FileMode.Create, FileAccess.Write))
        {
            fs.Write(ParquetMagic, 0, ParquetMagic.Length);

            var chunkStarts = new long[columns.Length];
            var chunkValueCounts = new long[columns.Length];
            var chunkTotalSizes = new long[columns.Length];

            for (int c = 0; c < columns.Length; c++)
            {
                chunkStarts[c] = fs.Position;
                long numValues = 0, totalSize = 0;

                for (int pageStart = 0; pageStart < columns[c].Values.Count; pageStart += ValuesPerPage)
                {
                    int count = Math.Min(ValuesPerPage, columns[c].Values.Count - pageStart);

                    // Plain 编码页体：每个 BYTE_ARRAY 值 = int32 LE 长度前缀 + 字节
                    byte[] body;
                    using (var bodyMs = new MemoryStream())
                    {
                        var lenBuf = new byte[4];
                        for (int i = pageStart; i < pageStart + count; i++)
                        {
                            byte[] v = columns[c].Values[i] ?? new byte[0];
                            WriteInt32Le(bodyMs, lenBuf, v.Length);
                            bodyMs.Write(v, 0, v.Length);
                        }
                        body = bodyMs.ToArray();
                    }

                    byte[] header = BuildPageHeader(count, body.Length);
                    fs.Write(header, 0, header.Length);
                    fs.Write(body, 0, body.Length);

                    numValues += count;
                    totalSize += header.Length + body.Length;
                }

                chunkValueCounts[c] = numValues;
                chunkTotalSizes[c] = totalSize;
            }

            byte[] footer = BuildFileMetaData(columns, rowCount, chunkStarts, chunkValueCounts, chunkTotalSizes);
            fs.Write(footer, 0, footer.Length);
            WriteInt32Le(fs, new byte[4], (int)footer.Length);
            fs.Write(ParquetMagic, 0, ParquetMagic.Length);
        }
    }

    private static void WriteInt32Le(Stream s, byte[] buf, int value)
    {
        buf[0] = (byte)value;
        buf[1] = (byte)(value >> 8);
        buf[2] = (byte)(value >> 16);
        buf[3] = (byte)(value >> 24);
        s.Write(buf, 0, 4);
    }

    // ================= Thrift Compact Protocol 写入 =================

    private const byte TCompactI32 = 5;
    private const byte TCompactI64 = 6;
    private const byte TCompactBinary = 8;
    private const byte TCompactList = 9;
    private const byte TCompactStruct = 12;

    /// <summary>Thrift Compact Protocol 序列化器（仅覆盖本文件用到的类型）。</summary>
    private sealed class CompactWriter
    {
        private readonly MemoryStream ms = new MemoryStream();
        private readonly Stack<int> fieldIdStack = new Stack<int>();
        private int lastFieldId;

        public byte[] ToArray() { return ms.ToArray(); }

        public void StructBegin()
        {
            fieldIdStack.Push(lastFieldId);
            lastFieldId = 0;
        }

        public void StructEnd()
        {
            ms.WriteByte(0); // STOP
            lastFieldId = fieldIdStack.Pop();
        }

        public void FieldBegin(short fieldId, byte type)
        {
            int delta = fieldId - lastFieldId;
            if (delta > 0 && delta <= 15)
            {
                ms.WriteByte((byte)((delta << 4) | type));
            }
            else
            {
                ms.WriteByte(type);
                WriteVarint(Zigzag(fieldId));
            }
            lastFieldId = fieldId;
        }

        public void I32(int v) { WriteVarint(Zigzag(v)); }
        public void I64(long v) { WriteVarint(Zigzag64(v)); }

        public void Binary(byte[] data)
        {
            WriteVarint((ulong)data.Length);
            ms.Write(data, 0, data.Length);
        }

        public void Binary(string s) { Binary(Encoding.UTF8.GetBytes(s)); }

        /// <summary>写列表头（元素类型 + 数量），随后逐个写元素（元素本身不再带类型头）。</summary>
        public void ListBegin(byte elemType, int count)
        {
            if (count <= 14)
            {
                ms.WriteByte((byte)((count << 4) | elemType));
            }
            else
            {
                ms.WriteByte((byte)(0xF0 | elemType));
                WriteVarint((ulong)count);
            }
        }

        private static ulong Zigzag(int v) { return (ulong)((v << 1) ^ (v >> 31)); }
        private static ulong Zigzag64(long v) { return (ulong)((v << 1) ^ (v >> 63)); }

        private void WriteVarint(ulong v)
        {
            while (true)
            {
                if (v < 0x80)
                {
                    ms.WriteByte((byte)v);
                    break;
                }
                ms.WriteByte((byte)((v & 0x7F) | 0x80));
                v >>= 7;
            }
        }
    }

    // ================= Parquet 元数据结构 =================

    private static byte[] BuildPageHeader(int numValues, int bodySize)
    {
        // struct PageHeader { 1: type; 2: uncompressed_page_size; 3: compressed_page_size;
        //                     5: data_page_header }
        // struct DataPageHeader { 1: num_values; 2: encoding; 3: definition_level_encoding;
        //                         4: repetition_level_encoding }
        var w = new CompactWriter();
        w.StructBegin();
        w.FieldBegin(1, TCompactI32); w.I32(PageTypeDataPage);
        w.FieldBegin(2, TCompactI32); w.I32(bodySize);
        w.FieldBegin(3, TCompactI32); w.I32(bodySize);
        w.FieldBegin(5, TCompactStruct);
        w.StructBegin();
        w.FieldBegin(1, TCompactI32); w.I32(numValues);
        w.FieldBegin(2, TCompactI32); w.I32(EncodingPlain);
        w.FieldBegin(3, TCompactI32); w.I32(EncodingRle);
        w.FieldBegin(4, TCompactI32); w.I32(EncodingRle);
        w.StructEnd();
        w.StructEnd();
        return w.ToArray();
    }

    private static byte[] BuildFileMetaData(Column[] columns, long rowCount,
        long[] chunkStarts, long[] chunkValueCounts, long[] chunkTotalSizes)
    {
        // struct FileMetaData {
        //   1: version; 2: schema (list<SchemaElement>); 3: num_rows;
        //   4: row_groups (list<RowGroup>); 6: created_by }
        var w = new CompactWriter();
        w.StructBegin();
        w.FieldBegin(1, TCompactI32); w.I32(1);

        // schema: 根元素 + 每列一个元素
        w.FieldBegin(2, TCompactList);
        w.ListBegin(TCompactStruct, columns.Length + 1);

        // 根元素（struct SchemaElement { 4: name; 5: num_children })
        w.StructBegin();
        w.FieldBegin(4, TCompactBinary); w.Binary("schema");
        w.FieldBegin(5, TCompactI32); w.I32(columns.Length);
        w.StructEnd();

        for (int c = 0; c < columns.Length; c++)
        {
            // struct SchemaElement { 1: type; 3: repetition_type; 4: name;
            //                        5: num_children; 6: converted_type }
            w.StructBegin();
            w.FieldBegin(1, TCompactI32); w.I32(TypeBinary);
            w.FieldBegin(3, TCompactI32); w.I32(RepetitionRequired);
            w.FieldBegin(4, TCompactBinary); w.Binary(columns[c].Name);
            if (columns[c].IsUtf8)
            {
                w.FieldBegin(6, TCompactI32); w.I32(ConvertedTypeUtf8);
            }
            w.StructEnd();
        }

        w.FieldBegin(3, TCompactI64); w.I64(rowCount);

        // row_groups: 单个 RowGroup
        w.FieldBegin(4, TCompactList);
        w.ListBegin(TCompactStruct, 1);

        // struct RowGroup { 1: columns (list<ColumnChunk>); 2: total_byte_size; 3: num_rows }
        w.StructBegin();
        w.FieldBegin(1, TCompactList);
        w.ListBegin(TCompactStruct, columns.Length);
        for (int c = 0; c < columns.Length; c++)
        {
            // struct ColumnChunk { 2: file_offset; 3: meta_data (ColumnMetaData) }
            w.StructBegin();
            w.FieldBegin(2, TCompactI64); w.I64(chunkStarts[c]);
            w.FieldBegin(3, TCompactStruct);

            // struct ColumnMetaData { 1: type; 2: encodings; 3: path_in_schema; 4: codec;
            //   5: num_values; 6: total_uncompressed_size; 7: total_compressed_size;
            //   9: data_page_offset }
            w.StructBegin();
            w.FieldBegin(1, TCompactI32); w.I32(TypeBinary);

            w.FieldBegin(2, TCompactList);
            w.ListBegin(TCompactI32, 2);
            w.I32(EncodingPlain);
            w.I32(EncodingRle);

            w.FieldBegin(3, TCompactList);
            w.ListBegin(TCompactBinary, 1);
            w.Binary(columns[c].Name);

            w.FieldBegin(4, TCompactI32); w.I32(CodecUncompressed);
            w.FieldBegin(5, TCompactI64); w.I64(chunkValueCounts[c]);
            w.FieldBegin(6, TCompactI64); w.I64(chunkTotalSizes[c]);
            w.FieldBegin(7, TCompactI64); w.I64(chunkTotalSizes[c]);
            w.FieldBegin(9, TCompactI64); w.I64(chunkStarts[c]);
            w.StructEnd(); // ColumnMetaData

            w.StructEnd(); // ColumnChunk
        }

        long totalBytes = 0;
        for (int c = 0; c < chunkTotalSizes.Length; c++) totalBytes += chunkTotalSizes[c];
        w.FieldBegin(2, TCompactI64); w.I64(totalBytes);
        w.FieldBegin(3, TCompactI64); w.I64(rowCount);
        w.StructEnd(); // RowGroup

        w.FieldBegin(6, TCompactBinary); w.Binary("GeometryScene MinimalParquetWriter");
        w.StructEnd(); // FileMetaData
        return w.ToArray();
    }
}
