using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Serialization.Formatters.Binary;

namespace Udpc.Share
{
    [Serializable]
    public class KnownFiles
    {
        public string FilePath;
        public DateTime LastEdit;
        public bool Deleted;
        public long Length;
    }
    
    public class NaiveFileShare : IFileShare
    {
        string directory;
        
        public void Init(string Directory)
        {
            if(directory != null) throw new InvalidOperationException();
            directory = Directory;
        }
        
        Dictionary<string, KnownFiles> allFiles = new Dictionary<string, KnownFiles>();

        public void Commit()
        {
            foreach (var entry in Directory.EnumerateFileSystemEntries(directory))
            {
                var f = new FileInfo(entry);
                allFiles[entry] = new KnownFiles() 
                    { FilePath = entry, LastEdit = f.LastWriteTimeUtc, Length = f.Attributes.HasFlag(FileAttributes.Directory) ? -1 :  f.Length};
            }
        }

        public object GetSyncData()
        {
            return allFiles;
        }

        public object GetSyncDiff(object remoteSync)
        {
            var files = (Dictionary<string, KnownFiles>) remoteSync;
            var changedfiles = new Dictionary<string, KnownFiles>();
            foreach (var file in files)
            {
                if (allFiles.ContainsKey(file.Key))
                {
                    if (allFiles[file.Key].LastEdit > file.Value.LastEdit)
                    {
                        changedfiles[file.Key] = file.Value;
                    }
                }
                else
                {
                    changedfiles[file.Key] = file.Value;
                }
            }
            return changedfiles;
        }

        public Stream GetSyncStream(object syncdata)
        {
            var files = (Dictionary<string, KnownFiles>) syncdata;
            return new SyncDataStream(files.Values.ToList());
        }

        public void UnpackSyncStream(Stream syncStream)
        {
            throw new NotImplementedException();
        }
    }

    public class SyncDataStream : Stream
    {
        List<KnownFiles> files;
        BinaryFormatter bf = new BinaryFormatter();
        MemoryStream header = new MemoryStream();
        Stream currentStream;

        public SyncDataStream(List<KnownFiles> files)
        {
            this.files = files;
            bf.Serialize(header, files);
            header.Seek(0, SeekOrigin.Begin);
            currentStream = header;
        }

        public override void Flush()
        {

        }

        int fileoffset = 0;

        Stream nextStream()
        {

            while (fileoffset < files.Count && files[fileoffset].Length == -1)
            {
                fileoffset++;
            }
            if (fileoffset < files.Count)
                return File.OpenRead(files[fileoffset++].FilePath);
            return null;
        }

        public override void Close()
        {
            base.Close();
            if (currentStream != null)
                currentStream.Close();
        }

        long position;

        public override int Read(byte[] buffer, int offset, int count)
        {
            if (currentStream == null) return 0;
            int written = currentStream.Read(buffer, offset, count);
            if (written < count)
            {
                currentStream.Dispose();
                currentStream = nextStream();
            }

            position += written;

            return written;
        }

        public override long Seek(long offset, SeekOrigin origin)
        {
            throw new InvalidOperationException();
        }

        public override void SetLength(long value)
        {
            throw new InvalidOperationException();
        }

        public override void Write(byte[] buffer, int offset, int count)
        {
            throw new InvalidOperationException();
        }

        public override bool CanRead { get; } = true;
        public override bool CanSeek { get; } = false;
        public override bool CanWrite { get; } = false;

        long length = 0;
        public override long Length => length;

        public override long Position
        {
            get { return position; }
            set { throw new InvalidOperationException(); }
        }
    }
}