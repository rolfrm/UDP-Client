using System;
using System.IO;
using Udpc.Share.Internal;

namespace Udpc.Share.DataLog
{
    public class NewFileLogItem : DataLogItem
    {
        public DateTime LastEdit { get; private set; }
        public long Size { get; private set; }

        public NewFileLogItem(DateTime lastEdit, long size) : this(Guid.NewGuid(), lastEdit, size)
        {
        }

        public NewFileLogItem(Guid id, DateTime lastEdit, long size) : base(id)
        {
            LastEdit = lastEdit;
            Size = size;
        }

        public override void Write(Stream stream)
        {
            stream.WriteLong(LastEdit.ToFileTimeUtc());
            stream.WriteLong(Size);
        }

        public override void Read(Stream stream)
        {
            LastEdit = DateTime.FromFileTimeUtc(stream.ReadLong());
            Size = stream.ReadLong();
        }
    }
}