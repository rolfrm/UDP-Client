﻿using System;
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
    public class FileConversation : Conversation
    {
        /// <summary>
        /// In bytes per second.
        /// </summary>
        public double TargetRate { get; set; } = 1e6;

        // Circular buffers/sums to keep track of amount of data sent and when.
        const int windowSize = 100;
        readonly CircularSum windowTransferred = new CircularSum(windowSize);
        readonly CircularSum windowStart = new CircularSum(windowSize);
        static readonly Stopwatch rateTimer = Stopwatch.StartNew();

        /// <summary>
        /// In bytes per second.
        /// </summary>
        public double CurrentRate { get; private set; }

        readonly ConversationManager manager;

        public FileConversation(ConversationManager manager)
        {
            this.manager = manager;
            bf = new BinaryFormatter();
            ms = new MemoryStream();
            windowStart.Add(rateTimer.ElapsedTicks);
        }

        protected void Send(byte[] data, int length = -1)
        {
            {   // Ensure that we are below the target transfer rate.
                checkTransferRate:

                var timenow = rateTimer.ElapsedTicks;
                var start = windowStart.First();
                double ts = (timenow - start) / Stopwatch.Frequency;
                CurrentRate = windowTransferred.Sum / ts;
                if (CurrentRate > TargetRate)
                {
                    Thread.Sleep(10);
                    goto checkTransferRate;
                }
                windowStart.Add(rateTimer.ElapsedTicks);
                windowTransferred.Add(length);
            }

            manager.SendMessage(this, data, length);

        }

        readonly BinaryFormatter bf;
        readonly MemoryStream ms;
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

        public bool ConversationFinished { get; protected set; }
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
        public int[] chunkIds;
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
        Stopwatch sw = new Stopwatch();
        bool isStarted = false;


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

        public override void Update()
        {
            if (sw.ElapsedMilliseconds > 500)
                ConversationFinished = true;
        }

        public override void HandleMessage(byte[] data)
        {
            sw.Reset();
            base.HandleMessage(data);
            var obj = ReadObject(data);
            if (obj == null) throw new InvalidOperationException("Un expected message from target.");
            byte[] buffer = new byte[1400];
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
                    foreach (var chunk in o.chunkIds)
                    {
                        buffer[0] = 2;
                        Utils.IntToByteArray(chunk, buffer, 1);
                        file.Seek(chunk * (1400 - 5), SeekOrigin.Begin);
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
        int chunksLeft;

        int finishedIndex = -1;
        bool unfinishedData = false;
        Stopwatch sw = new Stopwatch();
        public override void Update()
        {
            base.Update();
            if (unfinishedData && sw.ElapsedMilliseconds > 100)
            {
                List<int> indexes = new List<int>();
                for (int i = finishedIndex + 1; i < chunksToReceive.Length; i++)
                {
                    if (chunksToReceive[i] == false)
                    {
                        indexes.Add(i);
                    }


                    var data = indexes.ToArray();
                    if (data.Length == 0)
                    {
                        unfinishedData = false;
                    }
                    else
                    {
                        Send(new FileSendReq {chunkIds = data});
                    }
                }
                sw.Restart();
            }
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
                {
                    outStream.Close();
                    ConversationFinished = true;
                    Send(new ReceiveFinished());
                }
            }
            else if (data[0] == 1)
            {
                switch (ReadObject(data))
                {
                        case FileSendInfo rcvSendInfo:
                            sendInfo = rcvSendInfo;
                            Directory.CreateDirectory("Downloads");
                            tmpFilePath = Path.Combine("Downloads", sendInfo.FileName);
                            outStream = File.Open(tmpFilePath, FileMode.Create);
                            chunksLeft = (int)Math.Ceiling((double)sendInfo.Length / sendInfo.ChunkSize);
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

        public void Stop()
        {
            if(outStream != null)
                outStream.Close();
        }
    }

}