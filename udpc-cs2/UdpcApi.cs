using System;
using System.Runtime.InteropServices;
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

  public interface IUdpcClient
  {
    void Write(byte[] data, int length);
    int Read(byte[] buffer, int length);
    void Disconnect();
  }

  public interface IUdpcServer
  {
    void Disconnect();
    IUdpcClient Listen();
  }

  public static class Udpc
  {
    public static IUdpcClient Connect(string connectionString)
    {
      IntPtr cli = UdpcApi.udpc_connect(connectionString);
      if (cli == IntPtr.Zero) return null;
      return new UdpcClient(cli);
    }

    public static IUdpcServer Login(string connection)
    {
      var con = UdpcApi.udpc_login(connection);
      if (con == IntPtr.Zero) return null;
      return new UdpcServer(con);
    }
  }

  class UdpcClient : IUdpcClient
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

    public void Disconnect()
    {
      UdpcApi.udpc_close(con);
    }
  }

  class UdpcServer : IUdpcServer
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

    public IUdpcClient Listen()
    {
      if(con == IntPtr.Zero)
        throw new InvalidOperationException("UDPC server is disconnected");
      IntPtr cli = UdpcApi.udpc_listen(con);
      if (cli == IntPtr.Zero) return null;
      return new UdpcClient(cli);
    }
  }

  public class UdpcConversation
  {

    static int staticConversationId = 0;
    public int ConversationId = staticConversationId++;


  }
}