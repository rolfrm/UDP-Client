using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.Serialization.Formatters.Binary;
using System.Security.Cryptography;
using Blake2Sharp;
using Udpc.Share.Internal;

namespace Udpc.Share
{
    public class StreamEnumerator<T> : IEnumerable<T>, IDisposable
    {
        readonly IEnumerable<T> data;
        Stream dataStream;
        
        public StreamEnumerator(Stream dataStream, IEnumerable<T> data)
        {
            this.data = data;
            this.dataStream = dataStream;
        }

        public IEnumerator<T> GetEnumerator()
        {
            if (dataStream == null) throw new ObjectDisposedException("StreamEnumerator");
            return data.GetEnumerator();
        }

        IEnumerator IEnumerable.GetEnumerator()
        {
            return GetEnumerator();
        }

        public void Dispose()
        {
            if (dataStream == null) throw new ObjectDisposedException("StreamEnumerator");
            dataStream.Dispose();
            dataStream = null;
        }

        ~StreamEnumerator()
        {
            if(dataStream != null)
                Dispose();
        }
    }
    
    [Serializable]
    public class DataLogItem
    {
        public readonly Guid FileId;

        public DataLogItem(Guid id)
        {
            FileId = id;
        }
    }

    [Serializable]
    public class NewFileLogItem : DataLogItem
    {
        public readonly DateTime LastEdit;

        public NewFileLogItem(DateTime lastEdit) : this(Guid.NewGuid(), lastEdit)
        {
        }

        public NewFileLogItem(Guid id, DateTime lastEdit) : base(id)
        {
            this.LastEdit = lastEdit;
        }
    }

    [Serializable]
    public class NewDirectoryLogItem : DataLogItem
    {
        public NewDirectoryLogItem() : base(Guid.NewGuid())
        {
        }
    }

    [Serializable]
    public class FileNameLogItem : DataLogItem
    {
        public readonly string FileName;

        public FileNameLogItem(Guid itemid, string filename) : base(itemid)
        {
            FileName = filename;
        }

        public override string ToString()
        {
            return $"FileNameLogItem {FileName}";
        }
    }

    [Serializable]
    public class FileDataItem : DataLogItem
    {
        readonly public byte[] Content;
        readonly public long Offset;

        public FileDataItem(Guid itemid, byte[] content, long offset) : base(itemid)
        {
            Content = content;
            Offset = offset;
        }

        public override string ToString()
        {
            return $"FileData {Offset} {Content.Length}";
        }
    }

    [Serializable]
    public class DeletedFileItem : DataLogItem
    {
        public DeletedFileItem(Guid id) : base(id)
        {
        }
    }


    public struct DataLogFilePosition
    {
        public long DataPos;
        public long CommitPos;
    }

    public class DataLogFile : IDisposable
    {
        public readonly string DataFile;
        public readonly string CommitsFile;
        
        FileStream dataStream;
        FileStream commitStream;
        readonly BinaryFormatter bf = new BinaryFormatter();
        readonly MemoryStream mmstr = new MemoryStream();

        readonly Hasher hasher = Blake2B.Create();
        byte[] prevHash;
        
        //readonly SHA256 sha = new SHA256CryptoServiceProvider();

        public DataLogFile(string dataFile, string commitsFile)
        {
            this.DataFile = dataFile;
            this.CommitsFile = commitsFile;
        }
        
        public void writeItem(DataLogItem item)
        {
            mmstr.Position = 0;
            mmstr.SetLength(0);
            bf.Serialize(mmstr, item);
            mmstr.Position = 0;
            hasher.Init();
            Console.WriteLine("PRE HASH: {0} {1} {2} {3}",prevHash[0],prevHash[1],prevHash[2],prevHash[3]);
            hasher.Update(prevHash,0, 32);
            hasher.Update(mmstr.GetBuffer(),0, (int)mmstr.Length);
            mmstr.Position = 0;
            mmstr.CopyTo(dataStream);
            prevHash = hasher.Finish();
            Console.WriteLine("HASH: {0} {1} {2} {3}",prevHash[0],prevHash[1],prevHash[2],prevHash[3]);
            commitStream.Write(prevHash, 0, 32);
            byte[] lenbytes = BitConverter.GetBytes(mmstr.Length);
            commitStream.Write(lenbytes, 0, lenbytes.Length);
        }

        public DataLogFilePosition GetCurrentPosition()
        {
            return new DataLogFilePosition() {CommitPos = commitStream.Position, DataPos = dataStream.Position};
        }

        public void RestorePosition(DataLogFilePosition position)
        {
            dataStream.Position = position.DataPos;
            dataStream.SetLength(position.DataPos);

            commitStream.Position = position.CommitPos;
            commitStream.SetLength(position.CommitPos);
            
        }
        public void Flush()
        {
            dataStream.Flush();
            commitStream.Flush();
        }
        
        public void Open()
        {
            Utils.EnsureDirectoryExists(Path.GetDirectoryName(DataFile));
            dataStream = File.Open(DataFile, FileMode.OpenOrCreate, FileAccess.ReadWrite, System.IO.FileShare.Read);
            commitStream = File.Open(this.CommitsFile, FileMode.OpenOrCreate, FileAccess.ReadWrite, System.IO.FileShare.Read);
            if (commitStream.Length > 0)
            {
                var hsh = ReadCommitHashes().First();
                prevHash = hsh.GetHash();
            }
            else
            {
                prevHash = new byte[8 * 4];
            }
        }

        public IEnumerable<DataLogHash> ReadCommitHashes(Stream read)
        {
            int offset = 1;


            ulong[] conv = new ulong[5];
            var buffer = new byte[conv.Length * 8];
            while (true)
            {
                read.Seek(-offset * buffer.Length, SeekOrigin.End);
                offset += 1;
                bool dobreak = read.Position == 0;

                read.Read(buffer, 0, buffer.Length);
                Buffer.BlockCopy(buffer, 0, conv, 0, buffer.Length);

                yield return new DataLogHash {A = conv[0], B = conv[1], C = conv[2], D = conv[3], Length = conv[4]};

                if (dobreak)
                    break;
            }
        }
        
        
        public StreamEnumerator<DataLogHash> ReadCommitHashes()
        {
            var read = File.OpenRead(CommitsFile);
            return new StreamEnumerator<DataLogHash>(read, ReadCommitHashes(read));
            
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
            var length = lst.Select(x => (long)x.Length).Sum();
            dataStream.Position -= length;
            return ReadFrom(dataStream);
        }

        public static IEnumerable<DataLogItem> ReadFrom(Stream fstr)
        {
            BinaryFormatter bf = new BinaryFormatter();
            long pos = fstr.Position;
            while (pos < fstr.Length)
            {
                fstr.Seek(pos, SeekOrigin.Begin);
                var item = (DataLogItem) bf.Deserialize(fstr);
                pos = fstr.Position;
                yield return item;
            }
        }
        
        public static StreamEnumerator<DataLogItem> ReadFromFile(string filepath)
        {
            var fstr = File.OpenRead(filepath);
            return new StreamEnumerator<DataLogItem>(fstr, ReadFrom(fstr));
        }
        
    }
    
    public class DataLog : IDisposable
    {
        readonly string directory;
        public DataLogFile LogFile;

        
        readonly Dictionary<Guid, RegFile> file = new Dictionary<Guid, RegFile>();
        readonly Dictionary<string, Guid> fileNameToGuid = new Dictionary<string, Guid>();
        bool isInitialized;
        int iteration = 0;

        public DataLog(string directory, string dataFile, string commitsFile)
        {
            this.directory = directory.TrimEnd('\\').TrimEnd('/');
            LogFile = new DataLogFile(dataFile, commitsFile);
        }

        public void Unpack(IEnumerable<DataLogItem> items)
        {
            if(!isInitialized) throw new InvalidOperationException("DataLog is not initialized.");
            
           
            string translate(string path)
            {
                return Path.Combine(directory, path);
            }

            HashSet<Guid> guids = new HashSet<Guid>();
            
            foreach (var item in items)
            {
                switch (item)
                {
                    case NewFileLogItem f:
                        if (file.ContainsKey(f.FileId))
                        {
                            if(file[f.FileId].Name != null)
                                File.Delete(translate(file[f.FileId].Name));
                            file[f.FileId].LastEdit = f.LastEdit;
                        }
                        else
                        {
                            file[f.FileId] = new RegFile {IsFile = true, LastEdit = f.LastEdit};
                        }

                        break;
                    case NewDirectoryLogItem f:
                        file[f.FileId] = new RegFile {IsDirectory = true};
                        break;
                    case FileNameLogItem f:
                        if (file[f.FileId].Name != null)
                            File.Move(translate(file[f.FileId].Name), translate(f.FileName));
                        file[f.FileId].Name = f.FileName;
                        fileNameToGuid[Path.GetFullPath(translate(f.FileName))] = f.FileId;
                        if (file[f.FileId].IsDirectory)
                            Utils.EnsureDirectoryExists(translate(f.FileName));
                        break;
                    case FileDataItem f:
                        guids.Add(f.FileId);
                        var name = file[f.FileId].Name;
                        using (var fstr = File.Open(translate(name), FileMode.OpenOrCreate, FileAccess.Write))
                        {
                            fstr.Seek(f.Offset, SeekOrigin.Begin);
                            fstr.Write(f.Content, 0, f.Content.Length);
                        }

                        break;
                    case DeletedFileItem f:
                        File.Delete(translate(file[f.FileId].Name));
                        break;
                    default:
                        throw new NotImplementedException();
                }
                LogFile.writeItem(item);
                //register(item);
            }

            foreach (var id in guids)
            {
                var item = file[id];
                var finfo = new FileInfo(translate(item.Name));
                finfo.LastWriteTimeUtc = item.LastEdit;
            }
        }

        

        public void Update()
        {
            iteration += 1;
            if (!isInitialized)
            {
                
                isInitialized = true;
                Utils.EnsureDirectoryExists(directory);
                LogFile.Open();
                if (LogFile.IsEmpty)
                {
                    foreach (var item in Generate(directory))
                    {
                        LogFile.writeItem(item);
                        register(item);
                    }

                    return; // no need to do anything further
                }
                else
                {
                    foreach (var item in DataLogFile.ReadFromFile(LogFile.DataFile))
                    {
                        register(item);
                    }
                }
            }

            foreach (var item in Directory.EnumerateFileSystemEntries(directory, "*", SearchOption.AllDirectories))
            {
                var f = new FileInfo(item);

                Guid id;
                if (!fileNameToGuid.TryGetValue(f.FullName, out id))
                    id = Guid.Empty;
                else if (this.file[id].LastEdit >= f.LastWriteTimeUtc)
                {
                    this.file[id].Iteration = iteration;
                    continue;
                }


                IEnumerable<DataLogItem> logitems;

                if (id == Guid.Empty)
                    logitems = GenerateFromFile(directory, f.FullName);
                else
                    logitems = GenerateFileData(f.FullName, id);
                List<DataLogItem> itemsToRegister = new List<DataLogItem>();

                var pos = LogFile.GetCurrentPosition();
                
                try
                {
                    foreach (var x in logitems)
                    {
                        LogFile.writeItem(x);
                        itemsToRegister.Add(x);
                    }
                    itemsToRegister.ForEach(register);
                    this.file[itemsToRegister[0].FileId].Iteration = iteration;
                    
                }
                catch
                {
                    LogFile.RestorePosition(pos);
                }
            }

            foreach (var x in file)
            {
                if (x.Value.Iteration != iteration)
                {
                    if (x.Value.IsDeleted == false)
                    {
                        x.Value.IsDeleted = true;
                        LogFile.writeItem(new DeletedFileItem(x.Key));
                    }

                    x.Value.Iteration = iteration;
                }
            }
        }

        class RegFile
        {
            public string Name { get; set; }
            public bool IsFile;
            public bool IsDirectory;
            public bool IsDeleted;
            public DateTime LastEdit { get; set; }
            public int Iteration;
        }


        void register(DataLogItem item)
        {
            
            switch (item)
            {
                case NewFileLogItem f:
                    if(!file.ContainsKey(f.FileId))
                        file[f.FileId] = new RegFile {IsFile = true};
                    file[f.FileId].LastEdit = f.LastEdit;
                    break;
                case NewDirectoryLogItem f:
                    if(!file.ContainsKey(f.FileId))
                        file[f.FileId] = new RegFile {IsDirectory = true};
                    break;
                case FileNameLogItem f:
                    file[f.FileId].Name = Path.GetFullPath(Path.Combine(directory, f.FileName));
                    fileNameToGuid[file[f.FileId].Name] = f.FileId;
                    break;
            }
            file[item.FileId].Iteration = iteration;

        }

        static public IEnumerable<DataLogItem> GenerateFromFile(string directory, string item)
        {
            DataLogItem baseitem = null;
            var dstr = new DirectoryInfo(directory);
            var fstr = new FileInfo(item);
            if (fstr.Attributes.HasFlag(FileAttributes.Directory))
            {
                baseitem = new NewDirectoryLogItem();
                yield return baseitem;
            }
            else if (fstr.Attributes.HasFlag(FileAttributes.System) == false)
            {
                baseitem = new NewFileLogItem(fstr.LastWriteTimeUtc);
                yield return baseitem;
            }

            if (baseitem == null) yield break;
            yield return new FileNameLogItem(baseitem.FileId, fstr.FullName.Substring(dstr.FullName.Length + 1));
            if (baseitem is NewFileLogItem)
            {
                foreach (var item2 in GenerateFileData(item, baseitem.FileId))
                    yield return item2;
            }
        }

        static public IEnumerable<DataLogItem> GenerateFileData(string item, Guid id)
        {
            var fstr = new FileInfo(item);
            using (var str = fstr.Open(FileMode.Open, FileAccess.Read))
            {
                yield return new NewFileLogItem(id, fstr.LastWriteTimeUtc);
                while (true)
                {
                    byte[] chunk = new byte[Math.Max(1024 * 1024, fstr.Length)];
                    int read = str.Read(chunk, 0, chunk.Length);
                    if (read <= 0) break;
                    Array.Resize(ref chunk, read);
                    yield return new FileDataItem(id, chunk, str.Position - read);
                }
            }
        }

        static public IEnumerable<DataLogItem> Generate(string directory)
        {
            foreach (var item in Directory.EnumerateFileSystemEntries(directory, "*", SearchOption.AllDirectories))
            {
                foreach (var x in GenerateFromFile(directory, item))
                    yield return x;
            }
        }

        public IEnumerable<DataLogItem> GetItemsUntil(DataLogHash hash)
        {
            return LogFile.GetCommitsSince(hash);
        }


        public void Dispose()
        {
            LogFile.Dispose();
        }

        public void Flush()
        {
            LogFile.Flush();
        }
    }

    public struct DataLogHash
    {
        public ulong A, B, C, D; //32 bytes.
        public ulong Length;

        public override string ToString()
        {
            return string.Format("#{0:X}{1:X}{2:X}{3:X} ({4} bytes)", A, B, C, D, Length);
        }

        public byte[] GetHash()
        {
            byte[] hash = new byte[8 * 4];
            ulong[] src = new ulong[4] {A, B, C, D};
            Buffer.BlockCopy(src, 0, hash, 0, src.Length);
            return hash;
        }
    }

    
    public class DataLogMerge
    {
        
        static public bool MergeDataLogs(DataLog dest, DataLog src, int count)
        {
            var dest_hashes = dest.LogFile.ReadCommitHashes().Take(count).ToList();
            var src_hashes = src.LogFile.ReadCommitHashes().Take(count).ToList();
            int srcIdx = -1;
            int destIdx = 0;
            foreach (var h in dest_hashes)
            {
                srcIdx = src_hashes.IndexOf(h);
                if (srcIdx != -1)
                    break;
                destIdx++;
            }

            if (srcIdx <= 0)
            {
                //0: nothing new in src. -1: count is not enough.
                return count == 0;
            }
            
            

            var commonhash = src_hashes[srcIdx];


            var items = src.GetItemsUntil(commonhash);
            {

                if (destIdx > 0)
                {
                    throw new NotImplementedException();
                }

                dest.Unpack(items);
            }
            return true;
        }
    }
    
}