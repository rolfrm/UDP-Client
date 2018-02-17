using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Runtime.ExceptionServices;
using System.Runtime.Serialization.Formatters.Binary;
using System.Threading;
using System.Threading.Tasks;
using Udpc.Share.Internal;

namespace Udpc.Share
{
    public class FileShare
    {
        Thread shareThread;
        Udpc.IServer serv;
        bool shutdown;
        public string Service { get; private set; }
        public string DataFolder { get; private set; }
        List<ConversationManager> conversations = new List<ConversationManager>();
        Git git;
        
        public static FileShare Create(string service, string dataFolder)
        {
            dataFolder = Path.GetFullPath(dataFolder);
            Utils.EnsureDirectoryExists(dataFolder);

            var share = new FileShare {serv = Udpc.Login(service)};
            share.shareThread = new Thread(share.runShare);
            share.Service = service;
            share.DataFolder = dataFolder;
            share.shareThread.Start();
            share.git = new Git(dataFolder);
            
            void runListen()
            {
                var gitUpdate = Task.Factory.StartNew( () =>
                {
                    share.git.Init();
                    share.git.CommitAll();
                });
                while (!share.shutdown)
                {
                    var con = share.serv.Listen();
                    if (con != null)
                    {
                        gitUpdate.Wait();
                        share.go(() => share.onConnected(con, true));
                    }
                }
            }
            
            share.listenThread = new Thread(runListen);
            share.listenThread.Start();
            return share;
        }

        public void Stop()
        {
            shutdown = true;
        }

        public void ConnectTo(string service)
        {
            var cli = Udpc.Connect(service);
            for(int i = 0; i < 5 && cli == null; i++)
                cli = Udpc.Connect(service);
            onConnected(cli, false);
            
        }

        readonly ConcurrentQueue<Action> dispatcherQueue = new ConcurrentQueue<Action>();
        
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

                foreach (var conv in conversations.ToArray())
                    conv.Process();
                Thread.Sleep(10);
                
            }
            
            serv.Disconnect();
        }

        [Serializable]
        public class GitUpdate
        {
            public List<string> Data { get; set; }
        }
        
        [Serializable]
        public class SendMeDiff
        {
            public string CommonCommit { get; set; }
        }

        void onConnected(Udpc.IClient con, bool isServer)
        {
            var connectionManager = new ConversationManager(con, isServer);

            IConversation newConv(byte[] data)
            {
                if (data[4] == 0)
                {
                    Guid id = Guid.NewGuid();
                    var file = File.OpenWrite(".gitChunk" + id);
                    var conv = new ReceiveMessageConversation(connectionManager, file);
                    conv.Completed += (s, e) =>
                    {
                        go(() =>
                        {
                            git.ApplyPatch(file.Name);
                        });
                    };
                    return conv;
                }
                else if(data[4] == 1)
                {
                    var str = new MemoryStream();
                    var conv = new ReceiveMessageConversation(connectionManager, str);
                    conv.Completed += (s, e) =>
                    {
                        str.Seek(1, SeekOrigin.Begin);
                        var bf = new BinaryFormatter();
                        object thing = bf.Deserialize(str);
                        str.Dispose();
                        go(() => handleThing(connectionManager, thing));
                        
                    };
                    return conv;
                }
                return null;
            }

            connectionManager.NewConversation = newConv;
                
            conversations.Add(connectionManager);
            //CreateFileManager(connectionManager);
            //git.CommitAll();
            //var log = git.GetLog();
            //connectionManager.SendMessage(log);
        }

        public void UpdateIfNeeded()
        {
            if (git.GetGitStatus().Items.Count > 0)
            {
                go(() =>
                {
                    git.CommitAll();
                    var log = git.GetLog();
                    foreach (var conv in this.conversations)
                    {
                        conv.SendMessage(new GitUpdate{Data =  log});
                    }
                });
            }
        }

        void handleThing(ConversationManager conv, object thing)
        {
            switch (thing)
            {
                case GitUpdate upd:
                    Console.WriteLine("...");
                    string b = Git.GetCommonBase(upd.Data, git.GetLog());
                    conv.SendMessage(new SendMeDiff(){CommonCommit = b});
                    break;
                case SendMeDiff diff:
                    var f = git.GetPatch(diff.CommonCommit);
                    var fstr = File.OpenRead(f);
                    new SendMessageConversation(conv, fstr, null);
                    break;
                default:
                    throw new InvalidOperationException();
            }
        }

        const byte header = 0xF1;

        enum ConversationKinds : byte
        {
            CheckUpdates = 0x12,
            FetchPatch = 0x21
        }
        
        public void CreateFileManager(ConversationManager conv)
        {
            IConversation newConversation(byte[] bytes)
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


    public class AnyObjectSend : SendMessageConversation
    {
        
        static Stream getObjectStream(object obj)
        {
            var bf = new BinaryFormatter();
            var memstr = new MemoryStream();
            memstr.WriteByte(1);
            bf.Serialize(memstr, obj);
            memstr.Seek(0, SeekOrigin.Begin);
                
            return memstr;
        }
        
        public AnyObjectSend(ConversationManager manager, object anyThing) : base(manager, getObjectStream(anyThing), "")
        {
            
        }
        
        
    }

    public static class ConnectionExt
    {
        public static void SendMessage<T>(this ConversationManager man, T obj)
        {
           new AnyObjectSend(man, obj);
        }
    }
}