using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;
using Microsoft.VisualBasic.CompilerServices;

namespace udpc_cs2
{
  class UdpcApi {
    static UdpcApi()
    {
      dlopen("./libudpc_net.so", RTLD_NOW + RTLD_GLOBAL);
    }
    [DllImport("libdl.so")]
    static extern IntPtr dlopen(string filename, int flags);

    [DllImport("libdl.so")]
    static extern IntPtr dlsym(IntPtr handle, string symbol);

    const int RTLD_NOW = 2; // for dlopen's flags [DllImport("libdl.so")]
    const int RTLD_GLOBAL = 0x00100;

    [DllImport("libudpc_net.so")]
    public static extern void udpc_net_load();

    [DllImport("libudpc.so")]
    public static extern IntPtr udpc_login(string service);

    [DllImport("libudpc.so")]
    public static extern void udpc_logout(IntPtr con);

    [DllImport("libudpc.so")]
    public static extern void udpc_heartbeat(IntPtr con);

    [DllImport("libudpc.so")]
    public static extern IntPtr udpc_connect(string service);

    [DllImport("libudpc.so")]
    public static extern void udpc_close(IntPtr service);

    [DllImport("libudpc.so")]
    public static extern IntPtr udpc_listen(IntPtr con);

    [DllImport("libudpc.so")]
    public static extern void udpc_write(IntPtr con, byte[] buffer, ulong length);

    [DllImport("libudpc.so")]
    public static extern int udpc_read(IntPtr con, byte[] buffer, ulong size);

    [DllImport("libudpc.so")]
    public static extern int udpc_peek(IntPtr con, byte[] buffer, ulong size);

  }



  public static class Udpc
  {
    public static Client Connect(string connectionString)
    {
      IntPtr cli = UdpcApi.udpc_connect(connectionString);
      if (cli == IntPtr.Zero) return null;
      return new UdpcClient(cli);
    }

    public static Server Login(string connection)
    {
      var con = UdpcApi.udpc_login(connection);
      if (con == IntPtr.Zero) return null;
      return new UdpcServer(con);
    }

    public interface Client
    {
      void Write(byte[] data, int length);
      int Read(byte[] buffer, int length);
      int Peek(byte[] buffer, int length);
      void Disconnect();
    }

    public interface Server
    {
      void Disconnect();
      Client Listen();
    }
  }

  class UdpcClient : Udpc.Client
  {
    IntPtr con;

    public UdpcClient(IntPtr con)
    {
      this.con = con;
    }

    public void Write(byte[] data, int length)
    {
      UdpcApi.udpc_write(con, data, (ulong)length);
    }

    public int Read(byte[] buffer, int length)
    {
      return UdpcApi.udpc_read(con, buffer, (ulong)length);
    }

    public int Peek(byte[] buffer, int length)
    {
      return UdpcApi.udpc_peek(con, buffer, (ulong) length);
    }

    public void Disconnect()
    {
      UdpcApi.udpc_close(con);
    }
  }

  class UdpcServer : Udpc.Server
  {

    IntPtr con;
    public UdpcServer(IntPtr con)
    {
      this.con = con;
    }

    public void Disconnect()
    {
      if(con == IntPtr.Zero)
        throw new InvalidOperationException("Already disconnected");
      UdpcApi.udpc_logout(con);
      con = IntPtr.Zero;
    }

    public Udpc.Client Listen()
    {
      if(con == IntPtr.Zero)
        throw new InvalidOperationException("UDPC server is disconnected");
      IntPtr cli = UdpcApi.udpc_listen(con);
      if (cli == IntPtr.Zero) return null;
      return new UdpcClient(cli);
    }
  }

  public class ConversationManager
  {
    int conversationId = 0xF0;

    Udpc.Client cli;
    bool isServer = false;
    public ConversationManager(Udpc.Client cli, bool isServer)
    {
      this.cli = cli;
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

    Dictionary<int, Conversation> Conversations = new Dictionary<int, Conversation>();
    Dictionary<Conversation, int> ConversationIds = new Dictionary<Conversation, int>();

    public int GetConversationId(Conversation conv)
    {
      int id;
      if (ConversationIds.TryGetValue(conv, out id))
        return id;
      return -1;

    }

    public int StartConversation(Conversation conv)
    {
      var convId = newConversationId();

      Conversations[convId] = conv;
      ConversationIds[conv] = convId;

      return convId;
    }

    public Func<byte[], Conversation> NewConversation = (bytes)
      => throw new InvalidOperationException("Manager cannot create new conversations!");

    byte[] buffer = new byte[4];

    public void Process()
    {
      int l = cli.Peek(buffer, buffer.Length);
      if(l < 4) throw new InvalidOperationException("Invalid amount of data read.");
      int convId = BitConverter.ToInt32(buffer, 0);
      if(convId == 0 || convId == -1)
        throw new InvalidOperationException($"Invalid conversation ID: {convId}.");
      Conversation conv;
      if (!Conversations.TryGetValue(convId, out conv))
      {
        int mod = convId % 2;
        if ((isServer && mod == 0) || (!isServer && mod == 1))
        {
          conv = NewConversation(buffer);

          Conversations[convId] = conv;
          ConversationIds[conv] = convId;
        }
        else
        {
          throw new InvalidOperationException($"Conversation ID '{convId}' does not exist.");
        }
      }
      byte[] rawbuffer = new byte[l];
      cli.Read(rawbuffer, rawbuffer.Length);
      byte[] newbuffer = new byte[l - 4];
      Array.Copy(rawbuffer,4,newbuffer,0,l - 4);
      conv.HandleMessage(newbuffer);
    }

    byte[] sendBuffer = new byte[1024];

    public void SendMessage(Conversation conv, byte[] message)
    {
      int id;
      if(!ConversationIds.TryGetValue(conv, out id))
        throw new InvalidOperationException($"Unknown conversation");
      if(message.Length + 4 > sendBuffer.Length)
        Array.Resize(ref sendBuffer, message.Length + 4);
      Internal.Utils.IntToByteArray(id, sendBuffer, 0);
      Array.Copy(message, 0, sendBuffer, 4, message.Length);
      cli.Write(sendBuffer, message.Length + 4);
    }
  }

  public interface Conversation
  {
    void HandleMessage(byte[] data);
  }
}