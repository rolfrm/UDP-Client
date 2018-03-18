using System;
using System.IO;
using Udpc.Share.Internal;

namespace Udpc.Share.DataLog
{
    [Serializable]
    public struct DataLogHash
    {
        public ulong A, B, C, D; //32 bytes.
        public ulong Length;

        public override string ToString()
        {
            return $"#{A:X}{B:X}{C:X}{D:X} ({Length} bytes)";
        }

        public void ToStream(Stream str)
        {
            str.Write(new []{A,B,C,D,Length});
        }

        public const long Size = 5 * sizeof(ulong);

        public static DataLogHash Read(Stream str)
        {
            byte[] data = new byte[sizeof(ulong) * 5];
            str.Read(data);
            ulong[] ldata = new ulong[5];
            
            Buffer.BlockCopy(data, 0, ldata, 0, data.Length);
            return new DataLogHash
            {
                A = ldata[0],
                B = ldata[1],
                C = ldata[2],
                D = ldata[3],
                Length = ldata[4]
            };

        }
    

        public byte[] GetHash()
        {
            byte[] hash = new byte[8 * 4];
            ulong[] src = {A, B, C, D};
            Buffer.BlockCopy(src, 0, hash, 0, hash.Length);
            return hash;
        }
    }
}