using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Blake2Sharp;
using Udpc.Share.DataLog;
using Udpc.Share.Internal;

namespace Udpc.Share
{
    public class DataLogCore : IDisposable
    {
        public readonly string DataFile;
        readonly string commitsFile;
        public long CommitsCount { get; private set; }
        FileStream dataStream;
        FileStream commitStream;
        readonly MemoryStream mmstr = new MemoryStream();

        readonly Hasher hasher = Blake2B.Create();
        byte[] prevHash;

        public DataLogCore(string dataFile, string commitsFile)
        {
            DataFile = dataFile;
            this.commitsFile = commitsFile;
        }
        
        public void writeItem(DataLogItem item)
        {
            mmstr.Position = 0;
            mmstr.SetLength(0);
            item.ToStream(mmstr);
            mmstr.Position = 0;
            hasher.Init();
            hasher.Update(prevHash,0, 32);
            hasher.Update(mmstr.GetBuffer(),0, (int)mmstr.Length);
            mmstr.Position = 0;
            mmstr.CopyTo(dataStream);
            prevHash = hasher.Finish();
            commitStream.Write(prevHash, 0, 32);
            commitStream.WriteLong(mmstr.Length);
            CommitsCount += 1;
        }

        public DataLogFilePosition GetCurrentPosition()
        {
            return new DataLogFilePosition {CommitPos = commitStream.Position, DataPos = dataStream.Position, CommitsCount =  CommitsCount};
        }

        public void RestorePosition(DataLogFilePosition position)
        {
            dataStream.Position = position.DataPos;
            dataStream.SetLength(position.DataPos);

            commitStream.Position = position.CommitPos;
            commitStream.SetLength(position.CommitPos);
            CommitsCount = position.CommitsCount;
        }
        public void Flush()
        {
            dataStream.Flush();
            commitStream.Flush();
        }
        
        public void Open()
        {
            if(dataStream != null)
                throw new InvalidOperationException("DataLog is already open.");
            Utils.EnsureDirectoryExists(Path.GetDirectoryName(DataFile));
            dataStream = File.Open(DataFile, FileMode.OpenOrCreate, FileAccess.ReadWrite, System.IO.FileShare.Read);
            commitStream = File.Open(commitsFile, FileMode.OpenOrCreate, FileAccess.ReadWrite, System.IO.FileShare.Read);
            if (commitStream.Length > 0)
            {
                var hsh = ReadCommitHashes(0, 10).First();
                prevHash = hsh.GetHash();
                CommitsCount = commitStream.Length / (8 * 5);
            }
            else
            {
                prevHash = new byte[8 * 4];
            }
        }

        public void ReadCommitHashes(Stream read, long offset, long count, List<DataLogHash> output, bool reverse)
        {
            if (read.Length == 0)
                return;
            if(read.Length % (5 * 8) != 0)
                throw new InvalidOperationException();
            ulong[] conv = new ulong[5];
            var buffer = new byte[conv.Length * 8];

            if(reverse)
                offset -= count - 1;
            for(long i = 0; i < count; i++)
            {
                bool dobreak;
                if (reverse)
                {
                    read.Seek((offset + i) * buffer.Length, SeekOrigin.Begin);
                    dobreak = read.Position == read.Length;
                }
                else
                {
                    read.Seek(-(offset + i + 1) * buffer.Length, SeekOrigin.End);
                    dobreak = read.Position == 0;
                }

                

                read.Read(buffer, 0, buffer.Length);
                Buffer.BlockCopy(buffer, 0, conv, 0, buffer.Length);

                output.Add(new DataLogHash {A = conv[0], B = conv[1], C = conv[2], D = conv[3], Length = conv[4]});

                if (dobreak)
                    break;
            }
        }
        public void ReadCommitHashes(long offset, long count, List<DataLogHash> list, bool reverse = false)
        {
            using (var read = File.OpenRead(commitsFile))
                ReadCommitHashes(read, offset, count, list, reverse);
        }
        
        public List<DataLogHash> ReadCommitHashes(long offset, long count, bool reverse = false)
        {
            var list = new List<DataLogHash>();
            ReadCommitHashes(offset, count, list, reverse);
            return list;
        }

        public IEnumerable<DataLogHash> ReadCommitHashes()
        {
            Flush();
            const int batchCount = 20;
            var list = new List<DataLogHash>();
            long offset = 0;
            while (true)
            {
                ReadCommitHashes(offset, batchCount, list);
                foreach (var item in list)
                    yield return item;
                if (list.Count < batchCount)
                    yield break;
                offset += list.Count;
                list.Clear();
            }
        }

        public bool IsEmpty => dataStream.Length == 0;

        public void Dispose()
        {
            dataStream?.Dispose();
            commitStream?.Dispose();
            mmstr?.Dispose();
        }

        public IEnumerable<DataLogItem> GetCommitsSince(DataLogHash hash)
        {
            var lst = ReadCommitHashes().TakeWhile(x => x.Equals(hash) == false).ToList();
            var length = lst.Select(x => (long) x.Length).Sum();
            dataStream.Position -= length;
            try
            {
                return ReadFrom(dataStream).ToArray();
            }
            finally
            {
                dataStream.Position = dataStream.Length;
            }
                
        }
        
        public static IEnumerable<DataLogItem> ReadFrom(Stream fstr)
        {
            
            long pos = fstr.Position;
            while (pos < fstr.Length)
            {
                fstr.Seek(pos, SeekOrigin.Begin);
                var item = DataLogItem.FromStream(fstr);
                pos = fstr.Position;
                yield return item;
            }
        }
        
        public static StreamEnumerator<DataLogItem> ReadFromFile(string filepath)
        {
            var fstr = File.OpenRead(filepath);
            return new StreamEnumerator<DataLogItem>(fstr, ReadFrom(fstr));
        }

        public string CreatePatch(DataLogHash point, bool rewind)
        {
            long length = 0;
            bool foundsome = false;
            int commits = 0;
            foreach(var x in ReadCommitHashes())
            {
                if (x.Equals(point))
                {
                    foundsome = true;
                    break;
                }
                commits += 1;
                length += (long)x.Length;
            }

            if (foundsome == false) return null;
            
            dataStream.Position -= length;

            var tmp = Path.GetTempFileName();
            
            using (var f = File.Open(tmp, FileMode.Open))
            {
                dataStream.CopyTo(f);
                dataStream.Flush();
            }

            if (rewind)
            {
                dataStream.Position -= length;
                dataStream.SetLength(dataStream.Length - length);
                commitStream.Position -= commits * 5 * 8;
                commitStream.SetLength(commitStream.Length - commits * 5 * 8);
                prevHash = point.GetHash();
                CommitsCount -= commits;
            }

            return tmp;
        }
        
    }
}