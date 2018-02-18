using System;
using System.Runtime.InteropServices;

namespace Udpc.Share
{
  static class UdpcApi2 {
    static UdpcApi2()
    {
      udpc_init();
    }
    
    [DllImport("libudpc.so")]
    static extern IntPtr udpc_init();
    
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

    [DllImport("libudpc.so")]
    public static extern int udpc_pending(IntPtr con);

    [DllImport("libudpc.so")]
    public static extern void udpc_start_server(string address);

    [DllImport("libudpc.so")]
    public static extern void udpc_set_timeout(IntPtr con, int us);

    [DllImport("libudpc.so")]
    public static extern int udpc_get_timeout(IntPtr con);
  }

  class TraceDi : IDisposable
  {
    string thing;

    TraceDi(string thing)
    {
      //Console.WriteLine("Start {0}", thing);
      this.thing = thing;
    }
    public void Dispose()
    {
      //Console.WriteLine("Stop {0}", thing);
    }

    public static TraceDi Log(string thing)
    {
      return new TraceDi(thing);
    }
  }

  public static class UdpcApi
  {
    static object udpclock = new object();
    public static void udpc_net_load()
    {
      //UdpcApi2.udpc_net_load();
    }

    public static IntPtr udpc_login(string service)
    {
      using(TraceDi.Log(nameof(udpc_login))) 
      return UdpcApi2.udpc_login(service);
      
    }

    public static void udpc_logout(IntPtr con)
    {
      using(TraceDi.Log(nameof(udpc_logout)))
      UdpcApi2.udpc_logout(con);
    }

    public static void udpc_heartbeat(IntPtr con)
    {
      UdpcApi2.udpc_heartbeat(con);
    }

    public static IntPtr udpc_connect(string service)
    {
      using (TraceDi.Log(nameof(udpc_connect)))
        return UdpcApi2.udpc_connect(service);
    }

    public static void udpc_close(IntPtr service)
    {
      using(TraceDi.Log(nameof(udpc_close)))
      UdpcApi2.udpc_close(service);
    }

    public static IntPtr udpc_listen(IntPtr con)
    {
      using(TraceDi.Log(nameof(udpc_listen)))
      return UdpcApi2.udpc_listen(con);
    }

    public static void udpc_write(IntPtr con, byte[] buffer, ulong length)
    {
      using(TraceDi.Log(nameof(udpc_write)))
      UdpcApi2.udpc_write(con, buffer, length);
    }

    public static int udpc_read(IntPtr con, byte[] buffer, ulong size)
    {
      using(TraceDi.Log(nameof(udpc_read)))
      return UdpcApi2.udpc_read(con, buffer, size);
    }

    public static int udpc_peek(IntPtr con, byte[] buffer, ulong size)
    {
      using(TraceDi.Log(nameof(udpc_peek)))
      return UdpcApi2.udpc_peek(con, buffer, size);
    }

    public static int udpc_pending(IntPtr con)
    {
      using(TraceDi.Log(nameof(udpc_pending)))
      return UdpcApi2.udpc_pending(con);
    }

    public static void udpc_start_server(string address)
    {
      using(TraceDi.Log(nameof(udpc_start_server)))
      UdpcApi2.udpc_start_server(address);
    }

    public static void udpc_set_timeout(IntPtr con, int us)
    {
      using(TraceDi.Log(nameof(udpc_set_timeout)))
      UdpcApi2.udpc_set_timeout(con, us);
    }

    public static int udpc_get_timeout(IntPtr con)
    {
      using(TraceDi.Log(nameof(udpc_get_timeout)))
      return UdpcApi2.udpc_get_timeout(con);
    }
  }


  public static class Udpc
  {
    public static IClient Connect(string connectionString)
    {
      IntPtr cli = UdpcApi.udpc_connect(connectionString);
      return cli == IntPtr.Zero ? null : new UdpcClient(cli);
    }

    public static IServer Login(string connection)
    {
      var con = UdpcApi.udpc_login(connection);
      return con == IntPtr.Zero ? null : new UdpcServer(con);
    }

    public interface IClient
    {
      void Write(byte[] data, int length);
      int Read(byte[] buffer, int length);
      int Peek(byte[] buffer, int length);
      int Pending();
      void Disconnect();
      int TimeoutUs { get; set; }
    }

    public interface IServer
    {
      void Disconnect();
      IClient Listen();
    }
  }

  class UdpcClient : Udpc.IClient
  {
    readonly IntPtr con;

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

    public int Pending()
    {
      return UdpcApi.udpc_pending(con);
    }

    public void Disconnect()
    {
      UdpcApi.udpc_close(con);
    }

    public int TimeoutUs
    {
      get => UdpcApi.udpc_get_timeout(con);
      set => UdpcApi.udpc_set_timeout(con, value);
    }
  }

  class UdpcServer : Udpc.IServer
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

    public Udpc.IClient Listen()
    {
      if(con == IntPtr.Zero)
        throw new InvalidOperationException("UDPC server is disconnected");
      IntPtr cli = UdpcApi.udpc_listen(con);
      if (cli == IntPtr.Zero) return null;
      return new UdpcClient(cli);
    }
  }

  public interface IConversation
  {
    void HandleMessage(byte[] data);
    bool ConversationFinished { get; }
    void Update();
    byte Header { get; }
    void Start(ConversationManager manager);
  }
}
