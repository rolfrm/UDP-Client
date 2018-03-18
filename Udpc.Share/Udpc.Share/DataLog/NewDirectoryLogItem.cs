using System;
using System.IO;

namespace Udpc.Share.DataLog
{
    public class NewDirectoryLogItem : DataLogItem
    {
        public NewDirectoryLogItem() : base(Guid.NewGuid())
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