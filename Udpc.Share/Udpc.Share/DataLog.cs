using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.Serialization.Formatters.Binary;
using System.Security.Cryptography;
using Udpc.Share.Internal;

namespace Udpc.Share
{
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


    public class DataLog : IDisposable
    {
        readonly string directory;
        readonly string dataFile;
        readonly string commitsFile;

        readonly SHA256 sha = new SHA256CryptoServiceProvider();

        readonly Dictionary<Guid, RegFile> file = new Dictionary<Guid, RegFile>();
        readonly Dictionary<string, Guid> fileNameToGuid = new Dictionary<string, Guid>();
        bool isInitialized;
        int iteration = 0;

        FileStream dataStream;
        FileStream commitStream;
        readonly BinaryFormatter bf = new BinaryFormatter();
        readonly MemoryStream mmstr = new MemoryStream();
        

        public DataLog(string directory, string dataFile, string commitsFile)
        {
            this.directory = directory.TrimEnd('\\').TrimEnd('/');
            this.dataFile = dataFile;
            this.commitsFile = commitsFile;
        }

        public static IEnumerable<DataLogItem> ReadFromFile(string filepath)
        {
            using (var fstr = File.OpenRead(filepath))
            {
                BinaryFormatter bf = new BinaryFormatter();
                while (fstr.Position < fstr.Length)
                {
                    var item = (DataLogItem) bf.Deserialize(fstr);
                    yield return item;
                }
            }
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
                            file[f.FileId] = new RegFile() {IsFile = true, LastEdit = f.LastEdit};
                        }

                        break;
                    case NewDirectoryLogItem f:
                        file[f.FileId] = new RegFile() {IsDirectory = true};
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
                writeItem(item);
                //register(item);
            }

            foreach (var id in guids)
            {
                var item = file[id];
                var finfo = new FileInfo(translate(item.Name));
                finfo.LastWriteTimeUtc = item.LastEdit;
            }
        }



        void writeItem(DataLogItem item)
        {
            mmstr.Position = 0;
            mmstr.SetLength(0);
            bf.Serialize(mmstr, item);
            mmstr.Position = 0;
            sha.ComputeHash(mmstr);
            mmstr.Position = 0;
            mmstr.CopyTo(dataStream);
            commitStream.Write(sha.Hash, 0, sha.Hash.Length);
        }

        public void Flush()
        {
            dataStream.Flush();
            commitStream.Flush();
        }

        public void Update()
        {
            iteration += 1;
            if (!isInitialized)
            {
                isInitialized = true;
                Utils.EnsureDirectoryExists(directory);
                Utils.EnsureDirectoryExists(Path.GetDirectoryName(dataFile));
                dataStream = File.Open(dataFile, FileMode.OpenOrCreate, FileAccess.ReadWrite, System.IO.FileShare.Read);
                commitStream = File.Open(this.commitsFile, FileMode.OpenOrCreate, FileAccess.ReadWrite, System.IO.FileShare.Read);

                if (dataStream.Length == 0)
                {
                    foreach (var item in Generate(directory))
                    {
                        writeItem(item);
                        register(item);
                    }

                    return; // no need to do anything further
                }
                else
                {
                    foreach (var item in ReadFromFile(dataFile))
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

                var pos = dataStream.Position;
                var mpos = commitStream.Position;
                Console.WriteLine("POS: {0}", mpos);
                try
                {
                    foreach (var x in logitems)
                    {
                        writeItem(x);
                        itemsToRegister.Add(x);
                    }
                    itemsToRegister.ForEach(register);
                    this.file[itemsToRegister[0].FileId].Iteration = iteration;
                    
                }
                catch
                {
                    dataStream.Position = pos;
                    dataStream.SetLength(pos);
                    commitStream.Position = mpos;
                    commitStream.SetLength(mpos);
                }
            }

            foreach (var x in file)
            {
                if (x.Value.Iteration != iteration)
                {
                    if (x.Value.IsDeleted == false)
                    {
                        x.Value.IsDeleted = true;
                        writeItem(new DeletedFileItem(x.Key));
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
            yield return new FileNameLogItem(baseitem.FileId, item.Substring(directory.Length + 1));
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

        public IEnumerable<byte[]> ReadCommitHashes()
        {
            int offset = 1;

            using (var read = File.OpenRead(this.commitsFile))
            {
                while (true)
                {
                    read.Seek(-offset * 32, SeekOrigin.End);
                    offset += 1;
                    bool dobreak = read.Position == 0;
                    var buffer = new byte[32];
                    read.Read(buffer, 0, 32);
                    yield return buffer;

                    if (dobreak)
                        break;
                }
            }
        }

        public void Dispose()
        {
            sha?.Dispose();
            dataStream?.Dispose();
            commitStream?.Dispose();
            mmstr?.Dispose();
        }
    }

    public class DataLogMerge
    {
        public void MergeDataLogs(DataLog log1, DataLog log2)
        {
            
        }
    }
    
}