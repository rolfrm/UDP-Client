using System;
using System.IO;
using System.Runtime.Serialization;
using Udpc.Share.Internal;

namespace Udpc.Share.DataLog
{
    public abstract class DataLogItem
    {
        public Guid FileId { get; private set; }

        protected DataLogItem(Guid id)
        {
            FileId = id;
        }
        
        static readonly Type[] types = {
            typeof(NewFileLogItem), 
            typeof(NewDirectoryLogItem),
            typeof(FileNameLogItem),
            typeof(FileDataItem),
            typeof(NullFileLogItem),
            typeof(DeletedFileLogItem)
        };

        public abstract void Write(Stream stream);

        public abstract void Read(Stream stream);

        byte getId()
        {
            var tp = GetType();
            for (int i = 0; i < types.Length; i++)
            {
                if (types[i] == tp)
                    return (byte)(i + 1);
            }
            throw new InvalidOperationException("Unknown datalog type");
        }
        
        public void ToStream(Stream str)
        {
            str.WriteByte(getId());
            str.Write(FileId.ToByteArray());
            Write(str);
        }

        public static DataLogItem FromStream(Stream str)
        {
            int typeid = str.ReadByte();
            var type = types[typeid - 1];
            var obj = (DataLogItem)FormatterServices.GetUninitializedObject(type);
            obj.FileId = new Guid(str.ReadBytes(16));
            obj.Read(str);
            return obj;
        }

    }
}