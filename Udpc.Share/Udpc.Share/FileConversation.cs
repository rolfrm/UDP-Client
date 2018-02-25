using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.Serialization.Formatters.Binary;
using System.Threading;
using Udpc.Share.Internal;

namespace Udpc.Share
{
    /// <summary>
    /// Not thread-safe.
    /// </summary>
    public class FileConversation : IConversation
    {

        public byte Header { get; set; } = 5;
        
        public virtual void Start(ConversationManager manager)
        {
            this.manager = manager;
        }

        public const int CHUNK_SIZE = 10000; 
        
        ConversationManager manager;

        public FileConversation()
        {
            bf = new BinaryFormatter();
            ms = new MemoryStream();
            
        }

        protected void Send(byte[] data, int length = -1, bool isStart = false)
        {
 
            manager.SendMessage(this, data, length, isStart);

        }

        readonly BinaryFormatter bf;
        readonly MemoryStream ms;
        protected void Send(object obj, bool isStart = false)
        {
            ms.Seek(0, SeekOrigin.Begin);
            ms.SetLength(0);
            ms.WriteByte(1);
            
            bf.Serialize(ms, obj);
            var buf = ms.GetBuffer();
            Send(buf, (int)ms.Length, isStart);
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

        bool finished = false;
        public bool ConversationFinished
        {
            get => finished;
            protected set
            {
                if (finished != value)
                {
                    if(value == false) throw new InvalidOperationException("Conversation cannot be un-finished.");
                    finished = true;
                    if(Completed != null)
                        Completed(this, new EventArgs());
                }

            }
        }

        public event EventHandler Completed;
        
        public virtual void Update()
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
        public int[] ChunkIds;
    }

    [Serializable]
    public class ReceivedFileInfo
    {

    }

    [Serializable]
    public class ReceiveFinished
    {

    }

    /// <summary>
    /// Sends any size of message over a udpc channel.
    /// </summary>
    public class SendMessageConversation : FileConversation
    {
        readonly Stream file;
        readonly string fileName;
        readonly Stopwatch sw = new Stopwatch();
        bool isStarted;


        public SendMessageConversation(Stream fileToSend, string fileName)
        {
            file = fileToSend;
            this.fileName = fileName;
        }

        public override void Start(ConversationManager conv) 
        {
            base.Start(conv);
            var fsinfo = new FileSendInfo
            {
                ChunkSize =  CHUNK_SIZE - 5,
                FileName = fileName,
                Length = file.Length
            };
            Send(fsinfo, isStart: true);
        }

        public override void Update()
        {
            //if (sw.ElapsedMilliseconds > 500)
            //    ConversationFinished = true;
        }

        public override void HandleMessage(byte[] data)
        {
            sw.Reset();
            base.HandleMessage(data);
            var obj = ReadObject(data);
            if (obj == null) throw new InvalidOperationException("Unexpected message from target.");
            byte[] buffer = new byte[CHUNK_SIZE];
            switch (obj)
            {
                case ReceivedFileInfo _:
                {
                    isStarted = true;

                    int index = 0;
                    file.Seek(0, SeekOrigin.Begin);
                    int read;
                    while ((read = file.Read(buffer, 5, buffer.Length - 5)) > 0)
                    {
                        buffer[0] = 2;
                        Utils.IntToByteArray(index, buffer, 1);
                        Send(buffer, read + 5);

                        index += 1;
                    }

                    break;
                }
                case FileSendReq _:
                {
                    if(!isStarted)
                        throw new InvalidOperationException("Unexpected");
                    FileSendReq o = (FileSendReq) obj;
                    Console.WriteLine("Resend {0}", o.ChunkIds.Length);
                    foreach (var chunk in o.ChunkIds)
                    {
                        buffer[0] = 2;
                        Utils.IntToByteArray(chunk, buffer, 1);
                        file.Seek(chunk * (CHUNK_SIZE - 5), SeekOrigin.Begin);
                        int read = file.Read(buffer, 5, buffer.Length - 5);
                        Send(buffer, read + 5);
                    }
                }

                    break;
                case ReceiveFinished _:
                    if(!isStarted)
                        throw new InvalidOperationException("Unexpected");
                    ConversationFinished = true;
                    break;
                default:
                    throw new InvalidOperationException("Unsupported kind of object.");

            }

            sw.Start();
        }
        
    }

    public class ReceiveMessageConversation : FileConversation
    {
        FileSendInfo sendInfo;
        string tmpFilePath;
        Stream outStream;
        bool[] chunksToReceive;
        int chunksLeft = -1;

        int finishedIndex = -1;
        bool unfinishedData = false;
        readonly Stopwatch sw = Stopwatch.StartNew();
        public override void Update()
        {
            base.Update();
            if (ConversationFinished) return;
            if (chunksLeft == 0)
                Stop();

            if (!unfinishedData || sw.ElapsedMilliseconds <= 100) return;
            
            
            List<int> indexes = new List<int>();

            void sendIndexes()
            {
                var data = indexes.ToArray();
                if (data.Length == 0)
                    unfinishedData = false;
                else
                    Send(new FileSendReq {ChunkIds = data});
                indexes.Clear();
            }
            
            for (int i = finishedIndex + 1; i < chunksToReceive.Length; i++)
            {
                if (chunksToReceive[i] == false)
                    indexes.Add(i);

                if (indexes.Count < 200) continue;
                sendIndexes();
                sw.Restart();
                return;
            }
            sendIndexes();
            sw.Restart();
        }

        public override void HandleMessage(byte[] data)
        {
            if (data[0] == 2)
            {
                if(sendInfo == null)
                    throw new InvalidOperationException("SendInfo is not received yet.");
                int index = BitConverter.ToInt32(data, 1);
                if (chunksToReceive[index] == false)
                {
                    chunksToReceive[index] = true;
                    outStream.Seek(index * sendInfo.ChunkSize, SeekOrigin.Begin);
                    outStream.Write(data, 5, data.Length - 5);
                    chunksToReceive[index] = true;
                    chunksLeft--;
                    if (sw.IsRunning)
                        sw.Restart();

                    if (finishedIndex == index - 1)
                        finishedIndex = index;
                    else
                    {
                        unfinishedData = true;
                        if (sw.IsRunning == false)
                            sw.Start();
                    }
                }
                if (chunksLeft == 0)
                    Stop();

                
            }
            else if (data[0] == 1)
            {
                switch (ReadObject(data))
                {
                        case FileSendInfo rcvSendInfo:
                            sendInfo = rcvSendInfo;
                            if (streamOverride == null)
                            {
                                Directory.CreateDirectory("Downloads");
                                tmpFilePath = Path.Combine("Downloads", sendInfo.FileName);
                                outStream = new BufferedStream(File.Open(tmpFilePath, FileMode.Create), 1000000);
                            }
                            else
                            {
                                outStream = streamOverride;
                            }

                            chunksLeft = (int)Math.Ceiling((double)sendInfo.Length / sendInfo.ChunkSize);
                            chunksToReceive = new bool[chunksLeft];
                            Send(new ReceivedFileInfo());
                            break;
                        default:
                            throw new InvalidOperationException("Unknown object type");
                }
            }

        }
        

        readonly Stream streamOverride;
        public ReceiveMessageConversation(Stream outStream = null)
        {
            streamOverride = outStream;
            Header = 10;
        }

        public void Stop()
        {
            if (ConversationFinished) return;
            Send(new ReceiveFinished());
            ConversationFinished = true;
            if(streamOverride == null)
                outStream?.Close();
        }
    }

}