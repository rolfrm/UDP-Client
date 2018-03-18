using System;
using System.IO;
using System.Text;
using Udpc.Share.Internal;

namespace Udpc.Share.DataLog
{
    public class FileNameLogItem : DataLogItem
    {
        public string FileName { get; private set; }

        public FileNameLogItem(Guid itemid, string filename) : base(itemid)
        {
            FileName = filename;
        }

        public override string ToString()
        {
            return $"FileNameLogItem {FileName}";
        }

        public override void Write(Stream stream)
        {
            var bytes = Encoding.UTF8.GetBytes(FileName);
            stream.WriteLong(bytes.Length);
            stream.Write(bytes);
        }

        public override void Read(Stream stream)
        {
            var len = stream.ReadLong();
            var bytes = new byte[len];
            stream.Read(bytes);
            FileName = Encoding.UTF8.GetString(bytes);
        }
    }
}