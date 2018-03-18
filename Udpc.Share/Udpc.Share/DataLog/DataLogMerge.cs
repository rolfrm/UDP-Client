using System.IO;

namespace Udpc.Share.DataLog
{
    public static class DataLogMerge
    {
        
        static public void MergeDataLogs(Share.DataLog.DataLog dest, Share.DataLog.DataLog src, int count)
        {
            var destHashes = dest.LogCore.ReadCommitHashes(0, count);
            var srcHashes = src.LogCore.ReadCommitHashes(0, count);
            int srcIdx = -1;
            int destIdx = 0;
            foreach (var h in destHashes)
            {
                srcIdx = srcHashes.IndexOf(h);
                if (srcIdx != -1)
                    break;
                destIdx++;
            }

            if (srcIdx <= 0)
            {
                //0: nothing new in src. -1: count is not enough.
                return;
            }
            
            

            var commonhash = srcHashes[srcIdx];


            var items = src.GetItemsUntil(commonhash);
            {

                if (destIdx > 0)
                {
                    var patchfile = dest.LogCore.CreatePatch(commonhash, true);
                    dest.Unpack(items);
                    using (var pkg = DataLogCore.ReadFromFile(patchfile))
                    {
                        dest.Unpack(pkg);
                    }
                    File.Delete(patchfile);
                    
                }
                else
                {
                    dest.Unpack(items);
                }
            }
        }
    }
}