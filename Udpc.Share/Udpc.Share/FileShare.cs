using System;
using System.Collections.Concurrent;
using System.IO;
using System.Threading;
using Udpc.Share.Internal;

namespace Udpc.Share
{
    public class FileShare
    {
        Thread shareThread;
        Udpc.Server serv;
        bool shutdown;
        public string Service { get; private set; }
        public string DataFolder { get; private set; }
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
}