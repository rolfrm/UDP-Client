using System.IO;

namespace udpc_cs2.Internal
{
    public class Utils
    {
        static public void IntToByteArray(int x, byte[] out_array, int offset)
        {
            out_array[offset] = (byte) (x & 0xFF);
            out_array[offset + 1] = (byte) ((x >> 8) & 0xFF);
            out_array[offset + 2] = (byte) ((x >> 16) & 0xFF);
            out_array[offset + 3] = (byte) ((x >> 24) & 0xFF);
        }

        public static void EnsureDirectoryExists(string dataFolder)
        {
            Directory.CreateDirectory(dataFolder);
        }
    }
}