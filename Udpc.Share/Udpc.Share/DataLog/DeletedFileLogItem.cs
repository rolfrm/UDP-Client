using System;
using System.IO;

namespace Udpc.Share.DataLog
{
    public class DeletedFileLogItem : DataLogItem
    {
        public DeletedFileLogItem(Guid id) : base(id)
        {
        }

        public override void Write(Stream stream)
        {
        }

        public override void Read(Stream stream)
        {
        }
    }
}