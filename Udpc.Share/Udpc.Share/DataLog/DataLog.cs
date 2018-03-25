using System;
using System.Collections.Generic;
using System.IO;
using Udpc.Share.Internal;

namespace Udpc.Share.DataLog
{
    public class DataLog : IDisposable
    {
        readonly string directory;
        public readonly DataLogCore LogCore;
        
        readonly Dictionary<Guid, RegFile> file = new Dictionary<Guid, RegFile>();
        readonly Dictionary<string, Guid> fileNameToGuid = new Dictionary<string, Guid>();
        bool isInitialized;
        int iteration;

        public DataLog(string directory, string dataFile, string commitsFile)
        {
            this.directory = directory.TrimEnd('\\').TrimEnd('/');
            LogCore = new DataLogCore(dataFile, commitsFile);
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
                            file[f.FileId].Size = f.Size;
                        }
                        else
                        {
                            file[f.FileId] = new RegFile {IsFile = true, LastEdit = f.LastEdit, Size = f.Size};
                        }

                        break;
                    case NewDirectoryLogItem f:
                        file[f.FileId] = new RegFile {IsDirectory = true};
                        break;
                    case FileNameLogItem f:
                        if (file[f.FileId].Name != null)
                        { if(File.Exists(translate(file[f.FileId].Name)))
                            File.Move(translate(file[f.FileId].Name), translate(f.FileName));
                        }

                        file[f.FileId].Name = f.FileName;
                        fileNameToGuid[Path.GetFullPath(translate(f.FileName))] = f.FileId;
                        if (file[f.FileId].IsDirectory)
                            Utils.EnsureDirectoryExists(translate(f.FileName));
                        break;
                    case FileDataItem f:
                        guids.Add(f.FileId);
                        if(file[f.FileId].IsFile == false)
                            throw new InvalidOperationException();
                        var name = file[f.FileId].Name;
                        using (var fstr = File.Open(translate(name), FileMode.OpenOrCreate, FileAccess.Write))
                        {
                            fstr.Seek(f.Offset, SeekOrigin.Begin);
                            fstr.Write(f.Content, 0, f.Content.Length);
                        }

                        break;
                    case DeletedFileLogItem f:
                        if(file[f.FileId].IsFile == false)
                            File.Delete(translate(file[f.FileId].Name));
                        else
                        {
                            if(file[f.FileId].IsDirectory)
                                Directory.Delete(translate(file[f.FileId].Name), true);
                        }
                        break;
                    case NullFileLogItem _:
                        continue;
                    default:
                        throw new InvalidOperationException();
                }
                LogCore.writeItem(item);
            }

            foreach (var id in guids)
            {
                var item = file[id];
                File.SetLastWriteTimeUtc(translate(item.Name), item.LastEdit);
            }
            Flush();
        }

        

        public void Update()
        {
            iteration += 1;
            if (!isInitialized)
            {
                
                isInitialized = true;
                Utils.EnsureDirectoryExists(directory);
                LogCore.Open();
                if (LogCore.IsEmpty)
                {
                    Action<DataLogItem> f = x => { };

                    Generate(directory, x =>
                    {
                        LogCore.writeItem(x);
                        register(x);
                    });
            
                    Flush();

                    return; // no need to do anything further
                }
                
                using(var items = DataLogCore.ReadFromFile(LogCore.DataFile))
                   foreach (var item in items)
                      register(item);
            }
            
            foreach (var item in Directory.EnumerateFileSystemEntries(directory, "*", SearchOption.AllDirectories))
            {
                var f = new FileInfo(item);

                if (!fileNameToGuid.TryGetValue(f.FullName, out var id))
                    id = Guid.Empty;
                else if (file[id].LastEdit >= f.LastWriteTimeUtc)
                {
                    if (file[id].Size == f.Length)
                    {
                        file[id].Iteration = iteration;
                        continue;
                    }
                }else if (f.Attributes.HasFlag(FileAttributes.Directory))
                {
                    file[id].Iteration = iteration;
                    continue;
                }

                var fstr = new FileInfo(item);
                Stream str = null;
                if (fstr.Attributes.HasFlag(FileAttributes.Directory) == false)
                    str = fstr.Open(FileMode.Open, FileAccess.Read);
                try
                {
                    List<DataLogItem> itemsToRegister = new List<DataLogItem>();

                    void addItem(DataLogItem x)
                    {
                        LogCore.writeItem(x);
                        itemsToRegister.Add(x);
                    }

                    var pos = LogCore.GetCurrentPosition();

                        try
                        {
                            if (id == Guid.Empty || f.Attributes.HasFlag(FileAttributes.Directory))
                                GenerateFromFile(directory, f.FullName, str, addItem);
                            else
                                GenerateFileData(fstr, str, id, addItem);

                            itemsToRegister.ForEach(register);
                            file[itemsToRegister[0].FileId].Iteration = iteration;

                        }
                        catch
                        {
                            throw;
                            //LogCore.RestorePosition(pos);
                            
                        }
                    
                }
                finally
                {
                    str?.Close();
                }
            }

            foreach (var x in file)
            {
                if (x.Value.Iteration != iteration)
                {
                    if (x.Value.IsDeleted == false && x.Key != Guid.Empty)
                    {
                        x.Value.IsDeleted = true;
                        LogCore.writeItem(new DeletedFileLogItem(x.Key));
                    }

                    x.Value.Iteration = iteration;
                }
            }

            Flush();
        }

        class RegFile
        {
            public string Name { get; set; }
            public bool IsFile;
            public bool IsDirectory;
            public bool IsDeleted;
            public DateTime LastEdit { get; set; }
            public int Iteration;
            public long Size { get; set; }
        }


        void register(DataLogItem item)
        {
            
            switch (item)
            {
                case NewFileLogItem f:
                    if(!file.ContainsKey(f.FileId))
                        file[f.FileId] = new RegFile {IsFile = true, Size = f.Size};
                    file[f.FileId].LastEdit = f.LastEdit;
                    file[f.FileId].Size = f.Size;
                    break;
                case NewDirectoryLogItem f:
                    if(!file.ContainsKey(f.FileId))
                        file[f.FileId] = new RegFile {IsDirectory = true};
                    break;
                case FileNameLogItem f:
                    file[f.FileId].Name = Path.GetFullPath(Path.Combine(directory, f.FileName));
                    fileNameToGuid[file[f.FileId].Name] = f.FileId;
                    break;
                case NullFileLogItem f:
                    file[f.FileId] = new RegFile();
                    break;
            }
            file[item.FileId].Iteration = iteration;

        }

        static public void GenerateFromFile(string directory, string item, Stream str, Action<DataLogItem> process)
        {
            DataLogItem baseitem = null;
            var dstr = new DirectoryInfo(directory);
            var fstr = new FileInfo(item);
            if (fstr.Attributes.HasFlag(FileAttributes.Directory))
            {
                baseitem = new NewDirectoryLogItem();
                process(baseitem);
            }
            else if (fstr.Attributes.HasFlag(FileAttributes.System) == false)
            {
                baseitem = new NewFileLogItem(fstr.LastWriteTimeUtc, fstr.Length);
                process(baseitem);
            }

            if (baseitem == null) return;
            process(new FileNameLogItem(baseitem.FileId, fstr.FullName.Substring(dstr.FullName.Length + 1)));
            if (baseitem is NewFileLogItem)
                GenerateFileData(fstr, str, baseitem.FileId, process);
        }

        static public void GenerateFileData(FileInfo fstr, Stream str, Guid id, Action<DataLogItem> process)
        {
            process(new NewFileLogItem(id, fstr.LastWriteTimeUtc, fstr.Length));
            while (true)
            {
                byte[] chunk = new byte[Math.Min(1024 * 1024, fstr.Length)];
                int read = str.Read(chunk, 0, chunk.Length);
                if (read <= 0) break;
                Array.Resize(ref chunk, read);
                process(new FileDataItem(id, chunk, str.Position - read));
            }
            
        }

        static public void Generate(string directory, Action<DataLogItem> process)
        {
            process(new NullFileLogItem(Guid.Empty));
            foreach (var item in Directory.EnumerateFileSystemEntries(directory, "*", SearchOption.AllDirectories))
            {
                Stream str = null;
                var f = new FileInfo(item);
                if (f.Attributes.HasFlag(FileAttributes.Directory) == false)
                    str = File.OpenRead(item);
                try
                {
                    GenerateFromFile(directory, item, str, process);
                }
                finally
                {
                    str?.Dispose();
                }
            }
        }

        public IEnumerable<DataLogItem> GetItemsUntil(DataLogHash hash)
        {
            return LogCore.GetCommitsSince(hash);
        }


        public void Dispose()
        {
            Flush();
            LogCore.Dispose();
        }

        public void Flush()
        {
            LogCore.Flush();
        }
    }
}