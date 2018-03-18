using System;
using System.IO;

namespace Udpc.Share.Internal
{
    public class Utils
    {
        static public void IntToByteArray(int x, byte[] outArray, int offset)
        {
            outArray[offset] = (byte) (x & 0xFF);
            outArray[offset + 1] = (byte) ((x >> 8) & 0xFF);
            outArray[offset + 2] = (byte) ((x >> 16) & 0xFF);
            outArray[offset + 3] = (byte) ((x >> 24) & 0xFF);
        }

        public static void EnsureDirectoryExists(string dataFolder)
        {
            Directory.CreateDirectory(dataFolder);
        }
    }

    public static class Helpers
    {
        public static void Write(this Stream stream, byte[] data)
        {
            stream.Write(data, 0, data.Length);
        }
        
        public static void Write(this Stream stream, long[] data)
        {
            byte[] bytedata = new byte[data.Length * sizeof(long)];
            Buffer.BlockCopy(data, 0, bytedata, 0, bytedata.Length);
            stream.Write(bytedata);
        }
        
        public static void Write(this Stream stream, ulong[] data)
        {
            byte[] bytedata = new byte[data.Length * sizeof(ulong)];
            Buffer.BlockCopy(data, 0, bytedata, 0, bytedata.Length);
            stream.Write(bytedata);
        }

        public static void WriteLong(this Stream stream, long value)
        {
            stream.Write(BitConverter.GetBytes(value));
        }

        public static long Read(this Stream stream, byte[] buffer)
        {
            return stream.Read(buffer, 0, buffer.Length);
        }
        
        public static long ReadLong(this Stream stream)
        {
            byte[] data = new byte[8];
            stream.Read(data, 0, data.Length);
            return BitConverter.ToInt64(data, 0);
        }

        public static byte[] ReadBytes(this Stream stream, int count)
        {
            byte[] data = new byte[count];
            if(stream.Read(data, 0, count) != count)
                throw new InvalidOperationException();
            return data;
        }
        
    }
}