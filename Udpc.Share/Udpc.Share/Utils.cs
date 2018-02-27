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
}