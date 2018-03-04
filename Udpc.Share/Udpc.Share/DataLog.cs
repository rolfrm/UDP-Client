using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.Serialization.Formatters.Binary;
using Udpc.Share.Internal;

namespace Udpc.Share
{
    public enum DataLogItemType
    {
        NewFile,
        FileName,
        FileData,
        DeletedFile
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
        public DateTime LastEdit;

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


    public class DataLog
    {
        readonly string directory;
        readonly string datafile;
        readonly Dictionary<Guid, RegFile> file = new Dictionary<Guid, RegFile>();
        readonly Dictionary<string, Guid> fileNameToGuid = new Dictionary<string, Guid>();
        bool isInitialized;

        public DataLog(string directory, string datafile)
        {
            this.directory = directory.TrimEnd('\\').TrimEnd('/');
            this.datafile = datafile;
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

        public static void Unpack(string place, IEnumerable<DataLogItem> items)
        {
            Utils.EnsureDirectoryExists(place);

            string translate(string path)
            {
                return Path.Combine(place, path);
            }


            Dictionary<Guid, RegFile> filenames = new Dictionary<Guid, RegFile>();

            foreach (var item in items)
            {
                switch (item)
                {
                    case NewFileLogItem f:
                        if (filenames.ContainsKey(f.FileId))
                        {
                            File.Delete(translate(filenames[f.FileId].Name));
                            filenames[f.FileId].LastEdit = f.LastEdit;
                        }
                        else
                        {
                            filenames[f.FileId] = new RegFile() {IsFile = true, LastEdit = f.LastEdit};
                        }

                        break;
                    case NewDirectoryLogItem f:
                        filenames[f.FileId] = new RegFile() {IsDirectory = true};
                        break;
                    case FileNameLogItem f:
                        if (filenames[f.FileId].Name != null)
                            File.Move(translate(filenames[f.FileId].Name), translate(f.FileName));
                        filenames[f.FileId].Name = f.FileName;
                        if (filenames[f.FileId].IsDirectory)
                            Utils.EnsureDirectoryExists(translate(f.FileName));
                        break;
                    case FileDataItem f:
                        var name = filenames[f.FileId].Name;
                        using (var fstr = File.Open(translate(name), FileMode.OpenOrCreate, FileAccess.Write))
                        {
                            fstr.Seek(f.Offset, SeekOrigin.Begin);
                            fstr.Write(f.Content, 0, f.Content.Length);
                        }

                        break;
                    case DeletedFileItem f:
                        File.Delete(translate(filenames[f.FileId].Name));
                        break;
                    default:
                        throw new NotImplementedException();
                }
            }
        }

        int iteration = 0;

        public void Update()
        {
            BinaryFormatter bf = new BinaryFormatter();
            iteration += 1;
            if (!isInitialized)
            {
                isInitialized = true;
                if (!File.Exists(datafile))
                {
                    Utils.EnsureDirectoryExists(Path.GetDirectoryName(datafile));
                    using (var fstr = File.OpenWrite(datafile))
                    {
                        foreach (var item in Generate())
                        {
                            bf.Serialize(fstr, item);
                            register(item);
                        }
                    }

                    return; // no need to do anything further
                }
                else
                {
                    foreach (var item in ReadFromFile(datafile))
                    {
                        register(item);
                    }
                }
            }
            
            using (var fstr = File.Open(datafile, FileMode.Append, FileAccess.Write, System.IO.FileShare.Read))
            {
                foreach (var item in Directory.EnumerateFileSystemEntries(directory, "*", SearchOption.AllDirectories))
                {
                    var f = new FileInfo(item);

                    Guid id;
                    if (!fileNameToGuid.TryGetValue(f.FullName, out id))
                        id = Guid.Empty;
                    else if (this.file[id].LastEdit >= f.LastWriteTime)
                    {
                        this.file[id].Iteration = iteration;
                        continue;
                    }

                    
                    IEnumerable<DataLogItem> logitems;

                    if (id == Guid.Empty)
                        logitems = GenerateFromFile(f.FullName);
                    else
                        logitems = GenerateFileData(f.FullName, id);
                    List<DataLogItem> itemsToRegister = new List<DataLogItem>();

                    var pos = fstr.Position;
                    Console.WriteLine("POS: {0}", fstr.Position);
                    try
                    {
                        foreach (var x in logitems)
                        {
                            bf.Serialize(fstr, x);
                            itemsToRegister.Add(x);
                        }
                    }
                    catch
                    {
                        fstr.Position = pos;
                        fstr.SetLength(pos);
                    }

                    this.file[itemsToRegister[0].FileId].Iteration = iteration;
                    itemsToRegister.ForEach(register);
                }

                foreach (var x in file)
                {
                    if (x.Value.Iteration != iteration)
                    {
                        if (x.Value.IsDeleted == false)
                        {
                            x.Value.IsDeleted = true;
                            bf.Serialize(fstr, new DeletedFileItem(x.Key));
                        }

                        x.Value.Iteration = iteration;
                    }
                }
            }
        }

        class RegFile
        {
            public string Name;
            public bool IsFile;
            public bool IsDirectory;
            public bool IsDeleted;
            public DateTime LastEdit;
            public int Iteration;
        }


        void register(DataLogItem item)
        {
            switch (item)
            {
                case NewFileLogItem f:
                    file[f.FileId] = new RegFile {IsFile = true, LastEdit = f.LastEdit};
                    break;
                case NewDirectoryLogItem f:
                    file[f.FileId] = new RegFile {IsDirectory = true};
                    break;
                case FileNameLogItem f:
                    file[f.FileId].Name = Path.GetFullPath(Path.Combine(directory, f.FileName));
                    fileNameToGuid[file[f.FileId].Name] = f.FileId;
                    break;
                default:
                    break;
            }
        }

        public IEnumerable<DataLogItem> GenerateFromFile(string item)
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
                baseitem = new NewFileLogItem(fstr.LastAccessTime);
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

        public IEnumerable<DataLogItem> GenerateFileData(string item, Guid id)
        {
            var fstr = new FileInfo(item);
            using (var str = fstr.Open(FileMode.Open, FileAccess.Read))
            {
                yield return new NewFileLogItem(id, fstr.LastAccessTime);
                while (true)
                {
                    byte[] chunk = new byte[1024 * 8];
                    int read = str.Read(chunk, 0, chunk.Length);
                    if (read <= 0) break;
                    Array.Resize(ref chunk, read);
                    yield return new FileDataItem(id, chunk, str.Position - read);
                }
            }
        }

        public IEnumerable<DataLogItem> Generate()
        {
            foreach (var item in Directory.EnumerateFileSystemEntries(directory, "*", SearchOption.AllDirectories))
            {
                foreach (var x in GenerateFromFile(item))
                    yield return x;
            }
        }
    }
}