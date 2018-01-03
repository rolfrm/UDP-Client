using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
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

    void gitSuperPatch()
    {
      string[] dirs = new string[] {"sync_1", "sync_2", "sync_3"};
      Git[] gits = new Git[dirs.Length];
      int idx = 0;
      foreach (var dir in dirs)
      {
        try
        {
          Directory.Delete(dir, true);
        }
        catch
        {
        }

        Directory.CreateDirectory(dir);
        var git2 = new Git(dir);
        git2.Init();
        gits[idx] = git2;
        idx++;
      }

      while (true)
      {
        foreach (var git in gits)
        {
          if (git.CommitAll())
          {
            foreach (var git2 in gits)
            {
              if (git2 == git) continue;
              var commonBase = Git.GetCommonBase(git, git2);
              var patchfile1 = git.GetPatch(commonBase);
              git2.ApplyPatch(patchfile1);
            }
          }
        }
        Thread.Sleep(500);
        
      }
      
      
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
      readonly ConcurrentQueue<byte[]> readBuffer = new ConcurrentQueue<byte[]>();
      // the general propability of loosing a packet.
      public double LossPropability = 0.0;
      // The surpassing this limit will make it loose packets.
      public double MaxThroughPut = 1e6;
      
      // Circular buffers/sums to keep track of amount of data sent and when.
      const int windowSize = 100;
      readonly CircularSum windowTransferred = new CircularSum(windowSize);
      readonly CircularSum windowStart = new CircularSum(windowSize);
      static readonly Stopwatch rateTimer = Stopwatch.StartNew();
      double CurrentRate;  

      Random lossSimulationRnd = new Random();
      
      TestClient other;

        public static void CreateConnectionTest(out Udpc.Client c1, out Udpc.Client c2, double lossPropability)
        {

          var c11 = new TestClient {LossPropability = lossPropability};
          var c22 = new TestClient {LossPropability = lossPropability, other = c11};
          c11.other = c22;
          c1 = c11;
          c2 = c22;
        }

      public static void CreateConnectionUdp(out Udpc.Client c1, out Udpc.Client c2)
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
        
        {   // Ensure that we are below the target transfer rate.
          checkTransferRate:

          var timenow = rateTimer.ElapsedTicks;
          var start = windowStart.First();
          double ts = (timenow - start) / Stopwatch.Frequency;
          if (ts < 1.0)
          { // simulate that the buffers can hold one sec of data.
            if (windowTransferred.Sum > MaxThroughPut)
              return;
          }
          else
          {
            CurrentRate = windowTransferred.Sum / ts;
            if (CurrentRate > MaxThroughPut)
            {
              return; // loose the packet.
            }
          }
          
          windowStart.Add(rateTimer.ElapsedTicks);
          windowTransferred.Add(length);
        }
        var losscheck = lossSimulationRnd.NextDouble();
        if (losscheck < LossPropability)
          return; // simulate random packet loss.
        
        
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

      public bool ConversationFinished { get; }
      
      public void Update()
      {
        
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
      TestClient.CreateConnectionTest(out var c1, out var c2, 0);

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

    void TestShareFolders()
    {
      var share_1 = FileShare.Create("rolf@0.0.0.0:test_1", "sync_test_2");
      var share_2 = FileShare.Create("rolf@0.0.0.0:test_2", "sync_test_1");
      share_2.ConnectTo(share_1.Service);
      
    }


    void TestCircularSum()
    {
      var circ = new CircularSum(10);
      for (int i = 0; i < 20; i++)
      {
        circ.Add(i);
      }
      
      if(Math.Abs(circ.Sum - (10 + 11 + 12 + 13 + 14 + 15 + 16 + 17 + 18 + 19)) > 0.001)
        throw new InvalidOperationException("Error in algorithm");
      if(circ.Last() != 19)
        throw new InvalidOperationException("Error in algorithm");
      if(circ.First() != 10)
        throw new InvalidOperationException("Error in algorithm");  
    }

    void TestFileConversation(bool useUdp)
    {
      Console.WriteLine("TestFileConversation");
      Udpc.Client c1, c2;
      if (useUdp)
        TestClient.CreateConnectionUdp(out c1, out c2);
      else
        TestClient.CreateConnectionTest(out c1, out c2, 0.1);
      ReceiveMessageConversation rcv = null;
      var con1 = new ConversationManager(c1, true);
      con1.NewConversation = b => new ReceiveMessageConversation(con1);
      var con2 = new ConversationManager(c2, false);
      con2.NewConversation = b => rcv = new ReceiveMessageConversation(con2);

      byte[] bytes = new byte[123451];
      for (int i = 0; i < bytes.Length; i++)
        bytes[i] = (byte)(i % 3);
      var memstr = new MemoryStream(bytes);
      var snd = new SendMessageConversation(con1, memstr, "TestFile");
      
      con1.StartConversation(snd);
      Thread.Sleep(50);
      snd.Start();

      void runProcessing(ConversationManager con, string name)
      { 
        while (con.ConversationsActive)
        {
          if (con.Process()) 
            ; //sw.Restart();
          else
          {
            con.Update();
            Thread.Sleep(10);
          }

        }
      }

      var t1 = Task.Factory.StartNew(() => runProcessing(con1, "Send"));
      var t2 = Task.Factory.StartNew(() => runProcessing(con2, "Receive"));
      t1.Wait();
      t2.Wait();
      rcv.Stop();

      var data = File.ReadAllBytes("Downloads/TestFile");
      if (false == data.SequenceEqual(bytes))
      { 
        if(data.Length != bytes.Length)
          throw new InvalidOperationException("Lengths does not match.");
        for (int i = 0; i < data.Length; i++)
        {
          if(data[i] != bytes[i])
            throw new InvalidOperationException("Sequences are not equal");
          
        } 
      }
      Console.WriteLine("Download file done.");
    }

    public void RunTests()
    {
      TestFileConversation(false);
      return;
      
      TestCircularSum();
      //gitSuperPatch();
      GitInterop();
      TestUtils();
      var trd = new Thread(runServer) { IsBackground = true};
      trd.Start();
      Thread.Sleep(100);
      
      UdpcBasicInterop();
      UdpcSendFile();
      
      ConversationTest();
      
      TestFileConversation(true);
      Console.WriteLine("Tests finished");
    }

  }
}