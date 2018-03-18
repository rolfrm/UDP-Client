using System;
using System.IO;
using Udpc.Share.Internal;

namespace Udpc.Share.DataLog
{
    public class FileDataItem : DataLogItem
    {
        public byte[] Content { get; private set; }
        public long Offset { get; private set; }

        public FileDataItem(Guid itemid, byte[] content, long offset) : base(itemid)
        {
            Content = content;
            Offset = offset;
        }

        public override string ToString()
        {
            return $"FileData {Offset} {Content.Length}";
        }

        public override void Write(Stream stream)
        {
            stream.WriteLong(Offset);
            stream.WriteLong(Content.Length);
            stream.Write(Content);
        }

        public override void Read(Stream stream)
        {
            Offset = stream.ReadLong();
            Content = new byte[stream.ReadLong()];
            stream.Read(Content, 0, Content.Length);
        }
    }
}