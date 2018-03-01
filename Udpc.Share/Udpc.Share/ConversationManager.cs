using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;

namespace Udpc.Share
{
    public class ConversationManager
    {
        public static ConversationManager MultiplexWait(IEnumerable<ConversationManager> cms, int timeoutms)
        {
            var things = cms.Select(x => x.cli).ToArray();
            if (things.Length == 0)
            {
                Thread.Sleep(1);
                return null;
            }

            var cli = things[0].WaitReads(things, timeoutms);
            return cms.FirstOrDefault(x => x.cli == cli);
        }
        
        
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

        public int StartConversation(IConversation conv)
        {
            var convId = newConversationId();
            previousConversations.Add(convId);
            conversations[convId] = conv;
            conversationIds[conv] = convId;
            conv.Start(this);

            return convId;
        }

        public Func<byte, IConversation> NewConversation = (bytes)
            => throw new InvalidOperationException("Manager cannot create new conversations!");

        DateTime lastUpdate = DateTime.MinValue;
        TimeSpan latency = TimeSpan.Zero;
        byte[] buffer = new byte[8];
        
        public bool Process()
        {
            //processAgain:

            if (cli.WaitReads(new [] {cli}, 1) == null)
                return false;
            
            int l = cli.Peek(buffer, 5);

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
            if (convId == (isServer ? -2 : -3))
            { // special ping message
              // calculate latency
                cli.Read(buffer, buffer.Length);
                latency = DateTime.Now - lastUpdate;
                Console.WriteLine("Calculated latency {0}ms", latency.TotalMilliseconds);
                return true;
                //goto processAgain;
            }
            if (convId == (isServer ? -3 : -2))
            { // send back special ping message.
                cli.Read(buffer, buffer.Length);
                SendMessage(null, buffer);
                return true;
                //goto processAgain;
            }

            int offset = 4;
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
                    conv = NewConversation(buffer[4]);
                    
                    conversations[convId] = conv;
                    conversationIds[conv] = convId;
                    previousConversations.Add(convId);
                    conv.Start(this);
                    offset = 5;
                }
                else
                {
                    if(previousConversations.Contains(convId) == false)
                        throw new InvalidOperationException($"Conversation ID '{convId}' does not exist.");
                    // conversation was already closed.
                    return false;
          
                }
            }

            cli.Read(buffer, buffer.Length);
            byte[] newbuffer = new byte[l - offset];
            Array.Copy(buffer, offset, newbuffer, 0, l - offset);
            lock(processingItems)
                processingItems.Enqueue(() => conv.HandleMessage(newbuffer));

            if (DateTime.Now - lastUpdate > TimeSpan.FromMilliseconds(500))
            {
                lastUpdate = DateTime.Now;
                SendMessage(null, BitConverter.GetBytes((int)  (isServer ? -2 : -3)));
            }
            
            return true;
        }
        
        readonly Queue<Action> processingItems = new Queue<Action>();
        

        readonly HashSet<int> previousConversations = new HashSet<int>();

        public bool Update()
        {
            bool thingsHappened = false;
            while (true)
            {
                Action proc;
                lock (processingItems)
                    if (!processingItems.TryDequeue(out proc))
                        break;
                proc?.Invoke();
                thingsHappened = true;
            }
            
            foreach (var con in conversations.Values.Where(x => x.ConversationFinished).ToArray())
            {
                conversations.Remove(conversationIds[con]);
                conversationIds.Remove(con);
            }

            foreach (var con in conversations.Values)
            {
                con.Update();
            }
            return thingsHappened;
        }

        public bool ConversationsActive  => (cli.Pending() > 0) || (conversations.Values.Any( ));

        [ThreadStatic]
        static byte[] ssendBuffer;

        public void SendMessage(IConversation conv, byte[] message, int count = -1, bool isStart = false)
        {
            if (count == -1) count = message.Length;
            if(count > message.Length) throw new InvalidOperationException("Count cannot be larger than message.Length");
            
            var sendBuffer = ssendBuffer ?? new byte[1024];
            
            {   // Ensure that we are below the target transfer rate.
                //int retry = 0;
                checkTransferRate:
                //retry++;
                var timenow = rateTimer.ElapsedTicks;
                var start = windowStart.First();
                double ts = (timenow - start) / Stopwatch.Frequency;
                currentRate = (windowTransferred.Sum + message.Length) / ts;
                if (windowStart.Count > 5)
                {
                    if (currentRate > targetRate)
                    {
                        var waitTime = (windowTransferred.Sum + message.Length) / targetRate - ts;
                        var ticks = (long)(waitTime * TimeSpan.TicksPerSecond);
                        
                        if (waitTime > 0.001)
                        {
                            Thread.Sleep((int) (1000 * waitTime + 0.5));
                        }
                        else
                        {
                            Thread.SpinWait((int) (ticks * 20));
                        }

                        //Console.WriteLine("Should wait for {0} s ({1}) (({2})) {3}", waitTime, ticks, s.ElapsedTicks, retry);
                        goto checkTransferRate;                        
                    }
                }
                windowStart.Add(timenow);
                windowTransferred.Add(count + 4);
            }
            
            if (conv == null)
            {
                cli.Write(message, count);
                return;
            }
            
            if(!conversationIds.TryGetValue(conv, out int id))
                throw new InvalidOperationException($"Unknown conversation {conv}");
            if(count + 4 > sendBuffer.Length)
                Array.Resize(ref sendBuffer, count + 4 + 1);
            Internal.Utils.IntToByteArray(id, sendBuffer, 0);
            
            if (isStart)
            {
                Array.Copy(message, 0, sendBuffer, 5, count);
                sendBuffer[4] = conv.Header;
                cli.Write(sendBuffer, count + 5);
            }
            else
            {
                Array.Copy(message, 0, sendBuffer, 4, count);
                cli.Write(sendBuffer, count + 4);
            }

            ssendBuffer = sendBuffer;
        }
        
        /// <summary>
        /// In bytes per second.
        /// </summary>
        double targetRate { get; } = 10e7;
        
        // Circular buffers/sums to keep track of amount of data sent and when.
        const int windowSize = 100;
        readonly CircularSum windowTransferred = new CircularSum(windowSize);
        readonly CircularSum windowStart = new CircularSum(windowSize);
        static readonly Stopwatch rateTimer = Stopwatch.StartNew();
        
        /// <summary>
        /// In bytes per second.
        /// </summary>
        double currentRate { get; set; }
    }
}