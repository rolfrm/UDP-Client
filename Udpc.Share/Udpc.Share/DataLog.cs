﻿using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using Blake2Sharp;
using Udpc.Share.Internal;

namespace Udpc.Share
{
    public class StreamEnumerator<T> : IEnumerable<T>, IDisposable
    {
        readonly IEnumerable<T> data;
        Stream dataStream;

        public static StreamEnumerator<T> Empty()
        {
            return new StreamEnumerator<T>(null, null);
        }
        
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
            if (data == null) return;
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
    
    
    public abstract class DataLogItem
    {
        public Guid FileId { get; private set; }

        public DataLogItem(Guid id)
        {
            FileId = id;
        }
        
        static readonly Type[] types = {
            typeof(NewFileLogItem), 
            typeof(NewDirectoryLogItem),
            typeof(FileNameLogItem),
            typeof(FileDataItem),
            typeof(NullFileLogItem),
            typeof(DeletedFileLogItem)
        };

        public abstract void Write(Stream stream);

        public abstract void Read(Stream stream);

        byte getId()
        {
            var tp = GetType();
            for (int i = 0; i < types.Length; i++)
            {
                if (types[i] == tp)
                    return (byte)(i + 1);
            }
            throw new InvalidOperationException("Unknown datalog type");
        }
        
        public void ToStream(Stream str)
        {
            str.WriteByte(getId());
            str.Write(FileId.ToByteArray());
            Write(str);
        }

        public static DataLogItem FromStream(Stream str)
        {
            int typeid = (int)str.ReadByte();
            var type = types[typeid - 1];
            var obj = (DataLogItem)FormatterServices.GetUninitializedObject(type);
            obj.FileId = new Guid(str.ReadBytes(16));
            obj.Read(str);
            return obj;
        }

    }

    public class NewFileLogItem : DataLogItem
    {
        public DateTime LastEdit { get; private set; }
        public long Size { get; private set; }

        public NewFileLogItem(DateTime lastEdit, long size) : this(Guid.NewGuid(), lastEdit, size)
        {
        }

        public NewFileLogItem(Guid id, DateTime lastEdit, long size) : base(id)
        {
            this.LastEdit = lastEdit;
            this.Size = size;
        }

        public override void Write(Stream stream)
        {
            stream.WriteLong(LastEdit.ToFileTimeUtc());
            stream.WriteLong(Size);
        }

        public override void Read(Stream stream)
        {
            LastEdit = DateTime.FromFileTimeUtc(stream.ReadLong());
            Size = stream.ReadLong();
        }
    }

    public class NewDirectoryLogItem : DataLogItem
    {
        public NewDirectoryLogItem() : base(Guid.NewGuid())
        {
        }

        public override void Write(Stream stream)
        {
            
        }

        public override void Read(Stream stream)
        {
            
        }
    }

    public class FileNameLogItem : DataLogItem
    {
        public string FileName { get; private set; }

        public FileNameLogItem(Guid itemid, string filename) : base(itemid)
        {
            FileName = filename;
        }

        public override string ToString()
        {
            return $"FileNameLogItem {FileName}";
        }

        public override void Write(Stream stream)
        {
            var bytes = Encoding.UTF8.GetBytes(FileName);
            stream.WriteLong(bytes.Length);
            stream.Write(bytes);
        }

        public override void Read(Stream stream)
        {
            var len = stream.ReadLong();
            var bytes = new byte[len];
            stream.Read(bytes);
            FileName = Encoding.UTF8.GetString(bytes);
        }
    }

    public class FileDataItem : DataLogItem
    {
        public byte[] Content { get; private set; }
        public long Offset { get; private set; }

        public FileDataItem(Guid itemid, byte[] content, long offset) : base(itemid)
        {
            Content = content;
            Offset = offset;
        }

        public override string ToString()
        {
            return $"FileData {Offset} {Content.Length}";
        }

        public override void Write(Stream stream)
        {
            stream.WriteLong(Offset);
            stream.WriteLong(Content.Length);
            stream.Write(Content);
        }

        public override void Read(Stream stream)
        {
            Offset = stream.ReadLong();
            Content = new byte[stream.ReadLong()];
            stream.Read(Content, 0, Content.Length);
        }
    }

    public class DeletedFileLogItem : DataLogItem
    {
        public DeletedFileLogItem(Guid id) : base(id)
        {
        }

        public override void Write(Stream stream)
        {
        }

        public override void Read(Stream stream)
        {
        }
    }

    public class NullFileLogItem : DataLogItem
    {
        public NullFileLogItem (Guid id) : base(id)
        {
        }

        public override void Write(Stream stream)
        {
        }

        public override void Read(Stream stream)
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
        readonly MemoryStream mmstr = new MemoryStream();

        readonly Hasher hasher = Blake2B.Create();
        byte[] prevHash;
        
        //readonly SHA256 sha = new SHA256CryptoServiceProvider();

        public DataLogFile(string dataFile, string commitsFile)
        {
            DataFile = dataFile;
            CommitsFile = commitsFile;
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
            if(dataStream != null)
                throw new InvalidOperationException("DataLog is already open.");
            Utils.EnsureDirectoryExists(Path.GetDirectoryName(DataFile));
            dataStream = File.Open(DataFile, FileMode.OpenOrCreate, FileAccess.ReadWrite, System.IO.FileShare.Read);
            commitStream = File.Open(CommitsFile, FileMode.OpenOrCreate, FileAccess.ReadWrite, System.IO.FileShare.Read);
            if (commitStream.Length > 0)
            {
                var hsh = ReadCommitHashes(0, 10).First();
                prevHash = hsh.GetHash();
            }
            else
            {
                prevHash = new byte[8 * 4];
            }
        }

        public void ReadCommitHashes(Stream read, long offset, long count, List<DataLogHash> output)
        {
            if (read.Length == 0)
                return;
            if(read.Length % (5 * 8) != 0)
                throw new InvalidOperationException();
            ulong[] conv = new ulong[5];
            var buffer = new byte[conv.Length * 8];
   
            for(long i = 0; i < count; i++)
            {
                read.Seek(-(offset + i + 1)* buffer.Length, SeekOrigin.End);
                bool dobreak = read.Position == 0;

                read.Read(buffer, 0, buffer.Length);
                Buffer.BlockCopy(buffer, 0, conv, 0, buffer.Length);

                output.Add(new DataLogHash {A = conv[0], B = conv[1], C = conv[2], D = conv[3], Length = conv[4]});

                if (dobreak)
                    break;
            }
        }
        public void ReadCommitHashes(long offset, long count, List<DataLogHash> list)
        {
            using (var read = File.OpenRead(CommitsFile))
                ReadCommitHashes(read, offset, count, list);
        }
        
        public List<DataLogHash> ReadCommitHashes(long offset, long count)
        {
            var list = new List<DataLogHash>();
            ReadCommitHashes(offset, count, list);
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

        public string CreatePatch(DataLogHash point)
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
            dataStream.Position -= length;
            dataStream.SetLength(dataStream.Length - length);
            commitStream.Position -= commits * 5 * 8;
            commitStream.SetLength(commitStream.Length - commits * 5 * 8);
            this.prevHash = point.GetHash();
            return tmp;
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
                            file[f.FileId] = new RegFile {IsFile = true, LastEdit = f.LastEdit, Size = f.Size};
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
                    case DeletedFileLogItem f:
                        File.Delete(translate(file[f.FileId].Name));
                        break;
                    case NullFileLogItem f:
                        continue;
                    default:
                        throw new NotImplementedException();
                }
                LogFile.writeItem(item);
            }

            foreach (var id in guids)
            {
                var item = file[id];
                var finfo = new FileInfo(translate(item.Name));
                finfo.LastWriteTimeUtc = item.LastEdit;
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
                LogFile.Open();
                if (LogFile.IsEmpty)
                {
                    foreach (var item in Generate(directory))
                    {
                        LogFile.writeItem(item);
                        register(item);
                    }
                    Flush();

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
                else if (file[id].LastEdit >= f.LastWriteTimeUtc)
                {
                    if (file[id].Size == f.Length)
                    {
                        file[id].Iteration = iteration;
                        continue;
                    }
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
                    if (x.Value.IsDeleted == false && x.Key != Guid.Empty)
                    {
                        x.Value.IsDeleted = true;
                        LogFile.writeItem(new DeletedFileLogItem(x.Key));
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
                baseitem = new NewFileLogItem(fstr.LastWriteTimeUtc, fstr.Length);
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
                yield return new NewFileLogItem(id, fstr.LastWriteTimeUtc, fstr.Length);
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
            yield return new NullFileLogItem(Guid.Empty);
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
            Flush();
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
            var dest_hashes = dest.LogFile.ReadCommitHashes(0, count);
            var src_hashes = src.LogFile.ReadCommitHashes(0, count);
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
                    var patchfile = dest.LogFile.CreatePatch(commonhash);
                    dest.Unpack(items);
                    using (var pkg = DataLogFile.ReadFromFile(patchfile))
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
            return true;
        }
    }
    
}