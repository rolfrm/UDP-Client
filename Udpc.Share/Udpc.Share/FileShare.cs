using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
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
        public string Service { get; private set; }
        public string DataFolder { get; private set; }
        public bool ShutdownPending { get; private set; }

        readonly List<ConversationManager> conversations = new List<ConversationManager>();
        Git git;
        
        public static FileShare Create(string service, string dataFolder)
        {
            dataFolder = Path.GetFullPath(dataFolder);
            Utils.EnsureDirectoryExists(dataFolder);
            Console.WriteLine("----");
            Console.WriteLine("Connecting {1} to {0}", service, dataFolder);
            var serv = Udpc.Login(service);
            while (serv == null)
            {
                Thread.Sleep(1000);
                Console.WriteLine("Connecting {0}", service);
                serv = Udpc.Login(service);
                
            }

            var share = new FileShare {serv = Udpc.Login(service)};
            share.shareThread = new Thread(share.runShare);
            share.processThread = new Thread(share.runProcess);
            
            share.Service = service;
            share.DataFolder = dataFolder;
            share.shareThread.Start();
            share.processThread.Start();
            
            share.git = new Git(dataFolder);
            
            void runListen()
            {
                var gitUpdate = Task.Factory.StartNew( () =>
                {
                    share.git.Init();
                    share.git.CommitAll();
                });
                while (!share.ShutdownPending)
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
            ShutdownPending = true;
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
        Thread processThread;

        void runShare()
        {
            
            while (!ShutdownPending)
            {
                bool anyUpdates = false;
                if (dispatcherQueue.TryDequeue(out Action result))
                {
                    result();
                    anyUpdates = true;
                }

                foreach (var conv in conversations.ToArray())
                {
                    if (conv.Update())
                        anyUpdates = true;
                }
                if(!anyUpdates)
                    Thread.Sleep(1);
            }
            
            serv.Disconnect();
        }

        void runProcess()
        {
            while (!ShutdownPending)
            {
                if (conversations.Count == 0)
                {
                    Thread.Sleep(10);
                    continue;
                }

                var cm = ConversationManager.MultiplexWait(conversations, 100);
                cm?.Process();
            }

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

            IConversation newConv(byte header)
            {
                if (header == AnyObjectSend.Header2)
                {
                    var str = new MemoryStream();
                    var conv = new ReceiveMessageConversation(str);
                    conv.Completed += (s, e) =>
                    {
                        str.Seek(0, SeekOrigin.Begin);
                        var bf = new BinaryFormatter();
                        object thing = bf.Deserialize(str);
                        str.Dispose();
                        go(() => handleThing(connectionManager, thing));
                    };
                    return conv;
                }

                Guid id = Guid.NewGuid();
                var file = File.OpenWrite(".gitChunk" + id);
                var conv2 = new ReceiveMessageConversation(file);
                conv2.Completed += (s, e) => { go(() =>
                {
                    var fname = file.Name;
                    try
                    {
                        file.Close();
                        git.ApplyPatch(fname);
                    }
                    finally
                    {
                        File.Delete(fname);
                    }

                }); };
                return conv2;
            }

            connectionManager.NewConversation = newConv;
            conversations.Add(connectionManager);
        }

        public void WaitForProcessing()
        {
            while (!ShutdownPending && dispatcherQueue.Count > 0)
            {
                Thread.Sleep(100);
            }
        }

        public void UpdateIfNeeded()
        {
            if (ShutdownPending) return;
            //if (git.GetGitStatus().Items.Count > 0)
            {
                go(() =>
                {
                    git.CommitAll();
                    var log = git.GetLog();
                    foreach (var conv in conversations)
                    {
                        conv.SendMessage(new GitUpdate{Data = log});
                    }
                });
            }
        }

        void handleThing(ConversationManager conv, object thing)
        {
            switch (thing)
            {
                case GitUpdate upd:
                    var local = git.GetLog();
                    if (upd.Data.SequenceEqual(local))
                        break;
                    string b = Git.GetCommonBase(upd.Data, local);
                    conv.SendMessage(new SendMeDiff(){CommonCommit = b});
                    break;
                case SendMeDiff diff:
                    var local2 = git.GetLog();
                    if (local2.FirstOrDefault() == diff.CommonCommit)
                        break;
                    var f = git.GetPatch(diff.CommonCommit);
                    var fstr = File.OpenRead(f);
                    var c = new SendMessageConversation(fstr, null);
                    c.Completed += (s, e) =>
                    {
                        fstr.Close();
                        File.Delete(f);
                    };
                        
                    conv.StartConversation(c);
                    break;
                default:
                    throw new InvalidOperationException();
            }
        }
    }


    public class AnyObjectSend : SendMessageConversation
    {
        
        static Stream getObjectStream(object obj)
        {
            var bf = new BinaryFormatter();
            var memstr = new MemoryStream();
            bf.Serialize(memstr, obj);
            memstr.Seek(0, SeekOrigin.Begin);
                
            return memstr;
        }

        public const byte Header2 = 11;
        
        public AnyObjectSend(ConversationManager manager, object anyThing) : base(getObjectStream(anyThing), "")
        {
            Header = Header2;
        }
    }

    public static class ConnectionExt
    {
        public static void SendMessage<T>(this ConversationManager man, T obj)
        {
           man.StartConversation(new AnyObjectSend(man, obj));
        }
    }
}