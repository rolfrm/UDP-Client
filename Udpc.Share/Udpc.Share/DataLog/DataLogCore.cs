using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Blake2Sharp;
using Udpc.Share.Internal;

namespace Udpc.Share.DataLog
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

        bool checkRe = false;
        
        public void writeItem(DataLogItem item)
        {
            if(checkRe) throw new InvalidOperationException();
            checkRe = true;
            var cstream = commitStream;
            var dstream = dataStream;
            if(dstream.Length != dstream.Position)
                throw new InvalidOperationException(string.Format("writeItem: {0} {1}", dstream.Length, dstream.Position));
            commitStream = null;
            dataStream = null;
            try
            {
                mmstr.Position = 0;
                mmstr.SetLength(0);
                item.ToStream(mmstr);
                
                hasher.Init();
                hasher.Update(prevHash, 0, 32);
                hasher.Update(mmstr.GetBuffer(), 0, (int) mmstr.Length);
                dstream.Flush();
                var ppos = dstream.Length;
                mmstr.Position = 0;
                
                //Console.WriteLine("{0} A len: {1} pos:{2} data:{3}", commitsFile.GetHashCode(), dstream.Length, dstream.Position, mmstr.Length);
                
                mmstr.CopyTo(dstream);
                
                if (dstream.Length != ppos + mmstr.Length)
                {
                    Console.WriteLine("{0} B len: {1} pos:{2} data:{3}", commitsFile.GetHashCode(), dstream.Length, dstream.Position, mmstr.Length);    
                    throw new InvalidOperationException("This makes no sense!");
                }
                //Console.WriteLine("{0} C len: {1} pos:{2} data:{3}", commitsFile.GetHashCode(), dstream.Length, dstream.Position, mmstr.Length);

                prevHash = hasher.Finish();
                cstream.Write(prevHash, 0, 32);
                cstream.WriteLong(mmstr.Length);
                CommitsCount += 1;
                dstream.Flush();
                

                
            }
            finally
            {
                checkRe = false;
                commitStream = cstream;
                dataStream = dstream;
                if(dstream.Length != dstream.Position)
                    throw new InvalidOperationException();
            }

        }

        public DataLogFilePosition GetCurrentPosition()
        {
            return new DataLogFilePosition {CommitPos = commitStream.Position, DataPos = dataStream.Position, CommitsCount =  CommitsCount};
        }

        public void RestorePosition(DataLogFilePosition position)
        {
            //Console.WriteLine("Restore..");
            dataStream.Position = position.DataPos;
            dataStream.SetLength(position.DataPos);

            commitStream.Position = position.CommitPos;
            commitStream.SetLength(position.CommitPos);
            CommitsCount = position.CommitsCount;
        }
        public void Flush()
        {
            if (dataStream != null)
            {
                dataStream.Flush();
                commitStream.Flush();
            }
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
            
            if(dataStream.Length != dataStream.Position)
                throw new InvalidOperationException();
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
                if(dataStream.Length != dataStream.Position)
                    throw new InvalidOperationException();
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

        bool isCreatingPatch = false;

        public string CreatePatch(DataLogHash point, bool rewind)
        {
            if(isCreatingPatch)
                throw new InvalidOperationException();
            isCreatingPatch = true;
            Flush();
            var cstream = commitStream;
            var dstream = dataStream;
            commitStream = null;
            dataStream = null;
            try
            {
                //Console.WriteLine("Creating patch...");
                long length = 0;
                bool foundsome = false;
                int commits = 0;
                foreach (var x in ReadCommitHashes())
                {
                    if (x.Equals(point))
                    {
                        foundsome = true;
                        break;
                    }

                    commits += 1;
                    length += (long) x.Length;
                }

                if (foundsome == false)
                    return null;
                if (length == 0)
                    return null;
                //Console.WriteLine("Seek: {0} {1}", dstream.Position, length);
                var newpos = dstream.Position - length;
                //Console.WriteLine("seek: {0} {1} {2}", newpos, dstream.Length, commitsFile);
                //Console.WriteLine("{0} {1}", commitsFile.GetHashCode(), dstream.Length);
                //Console.Out.Flush();
                dstream.Seek(newpos, SeekOrigin.Begin);

                var tmp = Path.GetTempFileName();

                using (var f = File.Open(tmp, FileMode.Open))
                {
                    dstream.CopyTo(f);
                    dstream.Flush(true);
                    f.Flush(true);
                }

                if (rewind)
                {
                    dstream.Position -= length;
                    dstream.SetLength(dstream.Position);
                    cstream.Position -= commits * 5 * 8;
                    cstream.SetLength(cstream.Position);
                    prevHash = point.GetHash();
                    CommitsCount -= commits;
                }

                
                return tmp;
            }
            finally
            {
                //Console.WriteLine("Creating patch... Done");
                isCreatingPatch = false;
                commitStream = cstream;
                dataStream = dstream;
                if(dataStream.Length != dataStream.Position)
                    throw new InvalidOperationException(string.Format("seek: {0} {1} {2}", 1, dstream.Length, commitsFile));
            }
                
        }
        
    }
}