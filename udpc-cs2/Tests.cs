using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using udpc_cs2.Internal;

namespace udpc_cs2
{
  public class Tests
  {
    void GitInterop()
    {
      try
      {
        Directory.Delete("sync_1", true);
      }
      catch
      {
      }

      Directory.CreateDirectory("sync_1");
      File.WriteAllText("sync_1/test1", "Hello world");
      File.WriteAllText("sync_1/test2", "Hello world");

      var git1 = new Git("sync_1");
      git1.Init();
      var status = git1.GetGitStatus();
      git1.CommitAll();
      File.WriteAllText("sync_1/test1", "Hello world 222");
      var status2 = git1.GetGitStatus();
      git1.CommitAll();

      Directory.CreateDirectory("sync_1/test dir/");
      File.WriteAllText("sync_1/test dir/file.x", "hello");

      var status3 = git1.GetGitStatus();
      git1.CommitAll();

      File.WriteAllText("sync_1/test dir/file.x", "hello");

      var status4 = git1.GetGitStatus();
      git1.CommitAll();

      Directory.Delete("sync_1/test dir", true);

      var status5 = git1.GetGitStatus();
      git1.CommitAll();

      var log = git1.GetLog();

      try
      {
        Directory.Delete("sync_2", true);
      }
      catch
      {
      }

      Directory.CreateDirectory("sync_2");
      var git2 = new Git("sync_2");
      git2.Init();

      var commonBase = Git.GetCommonBase(git1, git2);

      var patchfile = git1.GetPatch(commonBase);
      // transfer file.

      git2.ApplyPatch(patchfile);
      File.Delete(patchfile);

      var log4 = git1.GetLog();
      var log5 = git2.GetLog();

      File.WriteAllText("sync_1/test2", "Hello world\nHello world\nHello world\n");
      git1.CommitAll();

      commonBase = Git.GetCommonBase(git1, git2);

      patchfile = git1.GetPatch(commonBase);
      // transfer file.

      git2.ApplyPatch(patchfile);
      File.Delete(patchfile);

      File.WriteAllText("sync_2/test2", "Hello world");
      git2.CommitAll();

      var log2 = git1.GetLog();
      var log3 = git2.GetLog();

      commonBase = Git.GetCommonBase(git1, git2);

      patchfile = git2.GetPatch(commonBase);
      git1.ApplyPatch(patchfile);
      File.Delete(patchfile);

      var log6 = git1.GetLog();
      var log7 = git2.GetLog();

      // test simultaneous edits
      File.WriteAllText("sync_2/test2", "Hello world 2");
      File.WriteAllText("sync_1/test1", "Hello world 5");

      git2.CommitAll();
      git1.CommitAll();

      commonBase = Git.GetCommonBase(git1, git2);
      var patchfile1 = git1.GetPatch(commonBase);
      //var patchfile2 = git2.GetPatch(commonBase);

      git2.ApplyPatch(patchfile1);
      var patchfile2 = git2.GetPatch(commonBase);
      git1.ApplyPatch(patchfile2);

      var log8 = git1.GetLog();
      var log9 = git2.GetLog();
    }

    void UdpcBasicInterop()
    {
      UdpcApi.udpc_net_load();
      IntPtr con = UdpcApi.udpc_login("rolf@0.0.0.0:test1");
      var tsk = Task.Factory.StartNew(() => {
        IntPtr c2 = UdpcApi.udpc_connect("rolf@0.0.0.0:test1");
        UdpcApi.udpc_write(c2, new byte[]{1,2,3}, 3);
        UdpcApi.udpc_write(c2, new byte[]{1,2,3}, 3);
        UdpcApi.udpc_write(c2, new byte[]{1,2,3}, 3);
        UdpcApi.udpc_close(c2);
      });

      IntPtr c = UdpcApi.udpc_listen(con);

      byte[] testbytes = new byte[3];
      for (int i = 0; i < 3; i++)
      {
        int cnt = UdpcApi.udpc_peek(c, testbytes, 0);
        int cnt2 = UdpcApi.udpc_pending(c);
        UdpcApi.udpc_read(c, testbytes, (ulong)testbytes.LongLength);
      }
      int cnt3 = UdpcApi.udpc_pending(c);


      tsk.Wait();
      Console.WriteLine("Got bytes {0} {1} {2}. {3}", testbytes[0], testbytes[1], testbytes[2],DateTime.Now);
      UdpcApi.udpc_logout(con);
      Console.WriteLine("Exit..");
    }

    void UdpcSendFile()
    {
      var serv = Udpc.Login("rolf@0.0.0.0:test1");
      var tsk = Task.Factory.StartNew(() => {
        var cli = Udpc.Connect("rolf@0.0.0.0:test1");
        var d = Enumerable.Range(0, 10000).Select(x => (byte)x).ToArray();
        cli.Write(d, d.Length);
        cli.Disconnect();
      });

      var tsk2 = Task.Factory.StartNew(() => {
        var cli = Udpc.Connect("rolf@0.0.0.0:test1");
        cli.Write(new byte[]{3,2,1}, 3);
        cli.Disconnect();
      });

      var s1 = serv.Listen();
      var s2 = serv.Listen();
      var testbytes = new byte[0];
      var testbytes2 = new byte[0];
      s1.Peek(testbytes, 0);
      int l1 = s1.Pending();
      s2.Peek(testbytes2, 0);
      int l2 = s2.Pending();

      testbytes = new byte[l1];
      testbytes2 = new byte[l2];


      s1.Read(testbytes, testbytes.Length);
      s2.Read(testbytes2, testbytes2.Length);
      tsk.Wait();
      tsk2.Wait();
      Console.WriteLine("Got bytes {0} {1} {2}. {3}", testbytes[0], testbytes[1], testbytes[2],DateTime.Now);
      Console.WriteLine("Got bytes {0} {1} {2}. {3}", testbytes2[0], testbytes2[1], testbytes2[2],DateTime.Now);
      s1.Disconnect();
      s2.Disconnect();
      serv.Disconnect();
      Console.WriteLine("Finished!");
    }

    void TestUtils()
    {
      int testInt = 0x11223344;
      byte[] bytes = new byte[8];
      Internal.Utils.IntToByteArray(testInt, bytes, 2);
      if(bytes[2] != 0x44 || bytes[3] != 0x33 || bytes[4] != 0x22 || bytes[5] != 0x11)
        throw new InvalidOperationException("IntToBytes not working.");
      int testInt2 = BitConverter.ToInt32(bytes, 2);
      if(testInt2 != testInt)
        throw new Exception("Cannot convert back.");
    }


    class TestClient : Udpc.Client
    {
      readonly Queue<byte[]> readBuffer = new Queue<byte[]>();

      TestClient other;

        public static void CreateConnection(out TestClient c1, out TestClient c2)
        {
          c1 = new TestClient();
          c2 = new TestClient();
          c1.other = c2;
          c2.other = c1;
        }

      public static void CreateConnection2(out Udpc.Client c1, out Udpc.Client c2)
      {
        var serv = Udpc.Login("rolf@0.0.0.0:test1");
        Udpc.Client c11 = null;
        var tsk = Task.Factory.StartNew(() =>
        {
          c11 = Udpc.Connect("rolf@0.0.0.0:test1");
        });
        c2 = serv.Listen();;
        tsk.Wait();
        c1 = c11;
      }

      public void Write(byte[] data, int length)
      {
        if(other == null) throw new InvalidOperationException("Disconnected");
        other.readBuffer.Enqueue(data.Take(length).ToArray());
      }

      public int Read(byte[] buffer, int length)
      {
        if(other == null) throw new InvalidOperationException("Disconnected");
        if (!readBuffer.TryDequeue(out byte[] buffer2))
          return 0;
        int read = Math.Min(length, buffer2.Length);
        Array.Copy(buffer2, buffer, read);
        return read;
      }

      public int Peek(byte[] buffer, int length)
      {
        if(other == null) throw new InvalidOperationException("Disconnected");
        var buffer2 = readBuffer.FirstOrDefault();
        if (buffer2 == null) return 0;
        int read = Math.Min(length, buffer2.Length);
        Array.Copy(buffer2, buffer, read);
        return read;
      }

      public int Pending()
      {
        return readBuffer.FirstOrDefault()?.Length ?? 0;
      }

      public void Disconnect()
      {
        other = null;
        readBuffer.Clear();
      }
    }

    class TestConversation : Conversation
    {
      readonly ConversationManager manager;
      public TestConversation(ConversationManager manager)
      {
        this.manager = manager;
      }

      public void HandleMessage(byte[] data)
      {
        int value = BitConverter.ToInt32(data, 0) + 1;
        Console.WriteLine("   >> {0}", value);
        Utils.IntToByteArray(value, data, 0);
        this.manager.SendMessage(this, data);
      }

      public void Start()
      {
        var data = new byte[4];
        Utils.IntToByteArray(1, data, 0);
        this.manager.SendMessage(this, data);
      }
    }

    void ConversationTest()
    {
      TestClient.CreateConnection2(out var c1, out var c2);

      var con1 = new ConversationManager(c1, true);
      con1.NewConversation = b => new TestConversation(con1);
      var con2 = new ConversationManager(c2, false);
      con2.NewConversation = b => new TestConversation(con2);

      var conv = new TestConversation(con1);
      con1.StartConversation(conv);

      conv.Start();

      var conv2 = new TestConversation(con2);
      con2.StartConversation(conv2);
      conv2.Start();

      for(int i = 0; i < 100; i++)
      {
        con1.Process();
        con2.Process();
      }
    }

    void runServer()
    {
      UdpcApi.udpc_start_server("0.0.0.0");
    }

    public void RunTests()
    {
      var trd = new Thread(runServer) { IsBackground = true};
      trd.Start();
      Thread.Sleep(100);
      //GitInterop();
      UdpcBasicInterop();
      UdpcSendFile();
      //TestUtils();
      ConversationTest();
    }

  }
}