using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Dynamic;
using System.IO;
using System.Runtime.InteropServices.ComTypes;
using System.Runtime.Serialization.Formatters.Binary;
using System.Threading;
using udpc_cs2.Internal;
namespace udpc_cs2
{
    /// <summary>
    /// This class uses git to have a version controlled list of files.
    /// </summary>
    public class FileManager
    {
        public readonly string Path;

        FileManager(string path)
        {
            this.Path = path;
        }

        public static FileManager Create(string path)
        {
            return new FileManager(path);
        }

        public void Update()
        {
        }
    }

    class Share
    {
        Thread shareThread;
        Udpc.Server serv;
        bool shutdown;
        public string Service { get; private set; }
        public string DataFolder { get; private set; }
        Git git;
        
        public static Share Create(string service, string dataFolder)
        {
            dataFolder = Path.GetFullPath(dataFolder);
            Utils.EnsureDirectoryExists(dataFolder);
            
            var share = new Share();
            share.serv = Udpc.Login(service);
            share.shareThread = new Thread(share.runShare);
            share.Service = service;
            share.DataFolder = dataFolder;
            share.shareThread.Start();
            share.git = new Git(dataFolder);
            
            void runListen()
            {
                while (!share.shutdown)
                {
                    var con = share.serv.Listen();
                    if (con != null)
                    {
                        share.go(() => share.onConnected(con, true));
                    }
                }
            }
            
            share.listenThread = new Thread(runListen);
            share.listenThread.Start();
            
            return share;
        }

        public void ConnectTo(string service)
        {
            var cli = Udpc.Connect(service);
            var conv = new ConversationManager(cli, false);
            CreateFileManager(conv);
        }
        
        ConcurrentQueue<Action> dispatcherQueue = new ConcurrentQueue<Action>();
        
        void go(Action f)
        {
            dispatcherQueue.Enqueue(f);
        } 
            
        Thread listenThread;
        void runShare()
        {
            
            while (!shutdown)
            {
                if (dispatcherQueue.TryDequeue(out Action result))
                {
                    result();
                }
            }
            
            serv.Disconnect();
        }

        void onConnected(Udpc.Client con, bool isServer)
        {
            var connectionManager = new ConversationManager(con, isServer);
            CreateFileManager(connectionManager);
        }

        const byte header = 0xF1;

        enum ConversationKinds : byte
        {
            CheckUpdates = 0x12,
            FetchPatch = 0x21
        }
        
        public void CreateFileManager(ConversationManager conv)
        {
            Conversation newConversation(byte[] bytes)
            {
                if (bytes[0] != header)
                    throw new InvalidOperationException("Unexpected data in start of conversation.");
                ConversationKinds kind = (ConversationKinds) bytes[1];
                switch (kind)
                {
                    case ConversationKinds.CheckUpdates:
                        throw new NotImplementedException();//return new GetCheckUpdatesConversation
                        
                }
            return null;
            }

            conv.NewConversation = newConversation;
            go(() =>
            {
                
            });

        }
    }

    public class CircularSum
    {
        public double Sum;
        double[] buffer;
        int count = 0;
        int front = 0;
        public CircularSum(int count)
        {
            buffer = new double[count];
        }

        int getTruePosition(int offset)
        {
            offset += front;
            if (offset >= count)
                offset -= count;
            return offset;
        }

        public double First()
        {
            int pos = getTruePosition(1);
            return buffer[pos];
        }

        public double Last()
        {
            int pos = getTruePosition(0);
            return buffer[pos];
        }

        public void Add(double value)
        {
            if (count < buffer.Length)
            {
                buffer[count] = value;
                count += 1;
                front += 1;
                Sum += value;
            }
            else
            {
                front = front + 1;
                if (front >= count)
                    front = 0;
                Sum = Sum - buffer[front] + (buffer[front] = value);
            }
            
        }
        
    }
    
    public class FileConversation : Conversation
    {
        protected double TargetRate = 1e6;
        CircularSum windowTransferred = new CircularSum(100);
        CircularSum windowStart = new CircularSum(100);
        
        public double CurrentRate = 0;
        readonly ConversationManager manager;
        
        static Stopwatch sw = Stopwatch.StartNew(); 
        
        public FileConversation(ConversationManager manager)
        {
            this.manager = manager;
            bf = new BinaryFormatter();
            windowStart.Add((double)sw.ElapsedTicks);
        }

        protected virtual void Send(byte[] data, int length = -1)
        {
            tryagain:
            var timenow = sw.ElapsedTicks;
            var start = windowStart.First();
            var ts = (double)(timenow - start) / Stopwatch.Frequency;
            CurrentRate = windowTransferred.Sum / ts;
            if (CurrentRate > TargetRate)
            {
                Thread.Sleep(10);
                goto tryagain;
            }

            manager.SendMessage(this, data, length);
            windowStart.Add(sw.ElapsedTicks);
            windowTransferred.Add(length);
        }

        BinaryFormatter bf;
        MemoryStream ms = new MemoryStream();
        protected void Send(object obj)
        {
            ms.Seek(0, SeekOrigin.Begin);
            ms.SetLength(0);
            ms.WriteByte(1);
            bf.Serialize(ms, obj);
            var buf = ms.GetBuffer();
            Send(buf, (int)ms.Length);   
        }

        protected object ReadObject(byte[] data)
        {
            if (data[0] != 1)
                return null;
            using (var ms2 = new MemoryStream(data))
            {
                ms2.Seek(1, SeekOrigin.Begin);
                return bf.Deserialize(ms2);
            }

        }
        public virtual void HandleMessage(byte[] data)
        {
            
        }
    }

    [Serializable]
    public class FileSendInfo
    {
        public long Length;
        public int ChunkSize;
        public string FileName;
    }

    [Serializable]
    public class FileSendReq
    {
        public int[] chunkIds;
    }

    [Serializable]
    public class ReceivedFileInfo
    {
        
    }

    /// <summary>
    /// Sends any size of message over a udpc channel.
    /// </summary>
    public class SendMessageConversation : FileConversation
    {
        Stream file;
        string fileName;
        
        public SendMessageConversation(ConversationManager manager, Stream fileToSend, string fileName) : base(manager)
        {
            file = fileToSend;
            this.fileName = fileName;
        }

        public void Start()
        {
            var fsinfo = new FileSendInfo
            {
                ChunkSize =  1400 - 5,
                FileName = fileName,
                Length = file.Length
            };
            Send(fsinfo);
        }

        public override void HandleMessage(byte[] data)
        {
            base.HandleMessage(data);
            var obj = ReadObject(data);
            if (obj == null) throw new InvalidOperationException("Un expected message from target.");
            byte[] buffer = new byte[1400];
            if (obj is ReceivedFileInfo)
            {
                int index = 0;
                file.Seek(0, SeekOrigin.Begin);
                int read = 0;
                while ((read = file.Read(buffer, 5, buffer.Length - 5)) > 0)
                {
                    buffer[0] = 2;
                    Utils.IntToByteArray(index, buffer, 1);
                    Send(buffer, read + 5);

                    index += 1;
                }
            }
            else if (obj is FileSendReq)
            {
                FileSendReq o = (FileSendReq) obj;
                foreach (var chunk in o.chunkIds)
                {
                    buffer[0] = 2;
                    file.Seek(chunk * (1400 - 5), SeekOrigin.Begin);
                    file.Read(buffer, 5, buffer.Length - 5);
                    Send(buffer);
                }
            }
        
        }
    }

    public class ReceiveMessageConversation : FileConversation
    {
        FileSendInfo SendInfo;
        string tmpFilePath;
        Stream outStream;
        bool[] chunksToReceive;
        int chunksLeft;
        public override void HandleMessage(byte[] data)
        {
            
            if (data[0] == 2)
            {
                if(SendInfo == null)
                    throw new InvalidOperationException("SendInfo is not received yet.");
                int index = BitConverter.ToInt32(data, 1);
                if (chunksToReceive[index] == false)
                {
                    chunksToReceive[index] = true;
                    outStream.Seek(index * SendInfo.ChunkSize, SeekOrigin.Begin);
                    outStream.Write(data, 5, data.Length - 5);
                    chunksToReceive[index] = true;
                    chunksLeft--;
                }

                if (chunksLeft == 0)
                {
                    outStream.Close();
                }
            }
            else if (data[0] == 1)
            {
                switch (ReadObject(data))
                {
                        case FileSendInfo sendInfo:
                            SendInfo = sendInfo;
                            Directory.CreateDirectory("Downloads");
                            tmpFilePath = Path.Combine("Downloads", SendInfo.FileName);
                            outStream = File.Open(tmpFilePath, FileMode.Create);
                            chunksLeft = (int)Math.Ceiling((double)SendInfo.Length / SendInfo.ChunkSize);
                            chunksToReceive = new bool[chunksLeft];
                            Send(new ReceivedFileInfo());
                            break;
                        default:
                            throw new InvalidOperationException("Unknown object type");
                }
            }
        }

        public ReceiveMessageConversation(ConversationManager manager) : base(manager)
        {
        }
    }

}