﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;

namespace Udpc.Share
{
    public class ConversationManager
    {
        int conversationId = 0xF0;

        readonly Udpc.IClient cli;
        readonly bool isServer;
        public ConversationManager(Udpc.IClient cli, bool isServer)
        {
            this.cli = cli;
            this.cli.TimeoutUs = 10000;
            if (isServer)
            {
                conversationId += 1;
                this.isServer = true;
            }
        }

        int newConversationId()
        {
            return conversationId += 2;
        }

        readonly Dictionary<int, IConversation> conversations = new Dictionary<int, IConversation>();
        readonly Dictionary<IConversation, int> conversationIds = new Dictionary<IConversation, int>();
        readonly Dictionary<IConversation, int> conversationTicks = new Dictionary<IConversation, int>();

        public int StartConversation(IConversation conv)
        {
            var convId = newConversationId();
            previousConversations.Add(convId);
            conversations[convId] = conv;
            conversationIds[conv] = convId;
            conversationTicks[conv] = 0;
            

            return convId;
        }

        public Func<byte[], IConversation> NewConversation = (bytes)
            => throw new InvalidOperationException("Manager cannot create new conversations!");

        byte[] buffer = new byte[8];
        public bool Process()
        {
            int l = cli.Peek(buffer, 8);

            if(l < 4)
                if (l <= 0)
                    return false;
                else
                    throw new InvalidOperationException("Invalid amount of data read.");
            l = cli.Pending();
            if(l > buffer.Length)
                Array.Resize(ref buffer, l);


            int convId = BitConverter.ToInt32(buffer, 0);
            if(convId == 0 || convId == -1)
                throw new InvalidOperationException($"Invalid conversation ID: {convId}.");
       
            if (!conversations.TryGetValue(convId, out IConversation conv))
            {
                int mod = convId % 2;
                if ((isServer && mod == 0) || (!isServer && mod == 1))
                {
                    if (previousConversations.Contains(convId))
                    {
                        cli.Read(buffer, buffer.Length);
                        return false;

                    }

                    conv = NewConversation(buffer);

                    conversations[convId] = conv;
                    conversationIds[conv] = convId;
                    previousConversations.Add(convId);
          
                }
                else
                {
                    if(previousConversations.Contains(convId) == false)
                        throw new InvalidOperationException($"Conversation ID '{convId}' does not exist.");
                    // conversation was already closed.
                    return false;
          
                }
            }
            conversationTicks[conv] = 0;

            cli.Read(buffer, buffer.Length);
            byte[] newbuffer = new byte[l - 4];
            Array.Copy(buffer, 4, newbuffer, 0, l - 4);
            conv.HandleMessage(newbuffer);
            return true;
        }

        readonly HashSet<int> previousConversations = new HashSet<int>();

        public void Update()
        {
            foreach (var con in conversations.Values.Where(x => x.ConversationFinished).ToArray())
            {
                conversations.Remove(conversationIds[con]);
                conversationIds.Remove(con);
            }

            foreach (var conv in conversationTicks.Keys.ToArray())
            {
                conversationTicks[conv]++;
            }
      
            foreach (var con in conversations.Values)
            {
                con.Update();
            }

            if (conversations.Values.Count == 0)
                Thread.Sleep(1);
        }

        public bool ConversationsActive  => (cli.Pending() > 0) || (conversationTicks.Values.Any( x => x < 10));

        byte[] sendBuffer = new byte[1024];

        public void SendMessage(IConversation conv, byte[] message, int count = -1)
        {
      
            if (count == -1) count = message.Length;
            if(count > message.Length) throw new InvalidOperationException("Count cannot be larger than message.Length");
      
            if(!conversationIds.TryGetValue(conv, out int id))
                throw new InvalidOperationException($"Unknown conversation");
            if(count + 4 > sendBuffer.Length)
                Array.Resize(ref sendBuffer, count + 4);
            Internal.Utils.IntToByteArray(id, sendBuffer, 0);
            Array.Copy(message, 0, sendBuffer, 4, count);
            cli.Write(sendBuffer, count + 4);
            conversationTicks[conv] = 0;
        }
    }
}