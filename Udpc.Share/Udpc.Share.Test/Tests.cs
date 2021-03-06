﻿using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Udpc.Share.DataLog;
using Udpc.Share.Internal;

namespace Udpc.Share.Test
{
  public class Tests
  {
   
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
    
    void UdpcLatencyTest()
    {
      UdpcApi.udpc_net_load();
      IntPtr con = UdpcApi.udpc_login("rolf@0.0.0.0:test1");
      int count = 1000;
      var tsk = Task.Factory.StartNew(() => {
        IntPtr c2 = UdpcApi.udpc_connect("rolf@0.0.0.0:test1");
        var sw = Stopwatch.StartNew();
        
        for (int i = 0; i < count; i++)
        {
          UdpcApi.udpc_write(c2, new byte[] {(byte)(1 + i), 2, 3}, 3);
          byte[] read = new byte[3];
          if(3 != UdpcApi.udpc_read(c2, read, (ulong) read.LongLength))
             throw new Exception("..");
          if((byte)(1 + i) != read[0])
            throw new Exception("...");
          
        }
        Console.WriteLine("Ping/pong test in {0}ms. {1}ms", sw.Elapsed.TotalMilliseconds, sw.Elapsed.TotalMilliseconds /count);

        UdpcApi.udpc_close(c2);
      });

      IntPtr c = UdpcApi.udpc_listen(con);

      byte[] testbytes = new byte[3];
      for (int i = 0; i < count; i++)
      {
        UdpcApi.udpc_read(c, testbytes, (ulong)testbytes.LongLength);
        UdpcApi.udpc_write(c, testbytes, (ulong)testbytes.LongLength);
      }
      int cnt3 = UdpcApi.udpc_pending(c);


      tsk.Wait();
      Console.WriteLine("Got bytes {0} {1} {2}. {3}", testbytes[0], testbytes[1], testbytes[2],DateTime.Now);
      UdpcApi.udpc_logout(con);
      Console.WriteLine("Exit..");
    }
    

    void UdpcAbstractTest()
    {
      var d = Enumerable.Range(0, 10000).Select(x => (byte) x).ToArray();
      var arr = new byte[] {3, 2, 1};
      for (int i = 0; i < 10; i++)
      {
        Console.WriteLine("Abstract Test {0}", i);
        var serv = Udpc.Login("rolf@0.0.0.0:test1");
        while(serv == null)
          serv = Udpc.Login("rolf@0.0.0.0:test1");
        //Thread.Sleep(20);
        var tsk = Task.Factory.StartNew(() =>
        {
          var cli = Udpc.Connect("rolf@0.0.0.0:test1");
          
          cli.Write(d, d.Length);
          cli.Disconnect();
        });

        var tsk2 = Task.Factory.StartNew(() =>
        {
          var cli = Udpc.Connect("rolf@0.0.0.0:test1");
          while(cli == null)
            cli = Udpc.Connect("rolf@0.0.0.0:test1");
          cli.Write(arr, 3);
          cli.Disconnect();
        });

        var s1 = serv.Listen();
        while (s1 == null)
          s1 = serv.Listen();
        var s2 = serv.Listen();
        while (s2 == null)
          s2 = serv.Listen();
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
        if (testbytes.Length > 2)
        {
          Console.WriteLine("Got bytes {0} {1} {2}. {3}", testbytes[0], testbytes[1], testbytes[2], DateTime.Now);
          Console.WriteLine("Got bytes {0} {1} {2}. {3}", testbytes2[0], testbytes2[1], testbytes2[2], DateTime.Now);
        }

        s1.Disconnect();
        s2.Disconnect();
        serv.Disconnect();
        Console.WriteLine("Finished!");
      }
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

    class TestClient : Udpc.IClient
    {
      
      
      readonly ConcurrentQueue<byte[]> readBuffer = new ConcurrentQueue<byte[]>();
      // the general propability of loosing a packet.
      public double LossPropability = 0.0;
      // The surpassing this limit will make it loose packets.
      public double MaxThroughPut = 1e8;

      // Circular buffers/sums to keep track of amount of data sent and when.
      const int windowSize = 100;
      readonly CircularSum windowTransferred = new CircularSum(windowSize);
      readonly CircularSum windowStart = new CircularSum(windowSize);
      static readonly Stopwatch rateTimer = Stopwatch.StartNew();
      double CurrentRate;

      Random lossSimulationRnd = new Random();

      TestClient other;

      TimeSpan timeout => TimeSpan.FromTicks((long)(TimeSpan.TicksPerSecond * TimeoutUs * 1e-6));

      public static void CreateConnectionTest(out Udpc.IClient c1, out Udpc.IClient c2, double lossPropability)
      {
        var c11 = new TestClient {LossPropability = lossPropability};
        var c22 = new TestClient {LossPropability = lossPropability, other = c11};
        c11.other = c22;
        c1 = c11;
        c2 = c22;
      }

      public static Udpc.IServer CreateConnectionUdp(out Udpc.IClient c1, out Udpc.IClient c2)
      {
        var serv = Udpc.Login("rolf@0.0.0.0:test1");
        Udpc.IClient c11 = null;
        var tsk = Task.Factory.StartNew(() =>
        {
          c11 = Udpc.Connect("rolf@0.0.0.0:test1");
        });
        c2 = serv.Listen();;
        tsk.Wait();
        c1 = c11;
        return serv;
      }

      public void Write(byte[] data, int length)
      {
        if(other == null) throw new InvalidOperationException("Disconnected");

        {   // Ensure that we are below the target transfer rate.

          var timenow = rateTimer.ElapsedTicks;
          if (windowStart.Count > 0)
          {
            var start = windowStart.First();
            double ts = (timenow - start) / Stopwatch.Frequency;
            if (ts < 1.0)
            {
              // simulate that the buffers can hold one sec of data.
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
        {

          Thread.Sleep(timeout);
          if (!readBuffer.TryDequeue(out buffer2))
            return 0;
        }

        int read = Math.Min(length, buffer2.Length);
        Array.Copy(buffer2, buffer, read);
        return read;
      }

      public int Peek(byte[] buffer, int length)
      {
        if(other == null) throw new InvalidOperationException("Disconnected");
        var buffer2 = readBuffer.FirstOrDefault();
        if (buffer2 == null)
        {
          Thread.Sleep(timeout);
          buffer2 = readBuffer.FirstOrDefault();
        }

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

      public int TimeoutUs { get; set; }
      public Udpc.IClient WaitReads(Udpc.IClient[] clients, int timeoutms)
      {
        var sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds > timeoutms)
        {

          foreach (TestClient cli in clients)
          {
            if (cli.readBuffer.Any())
            {
              return cli;
            }
          }
          Thread.Sleep(10);
        }
        return null;
      }
    }

    class TestConversation : IConversation
    {
      ConversationManager manager;
      
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

      public byte Header => 2;
      public void Start(ConversationManager manager)
      {
        this.manager = manager;
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
      con1.NewConversation = b => new TestConversation();
      var con2 = new ConversationManager(c2, false);
      con2.NewConversation = b => new TestConversation();

      var conv = new TestConversation();
      con1.StartConversation(conv);

      var conv2 = new TestConversation();
      con2.StartConversation(conv2);

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
        if(i > 10 && (i - 10) != circ.First() )
          throw new InvalidOperationException();
        circ.Add(i);
        if(i != circ.Last() )
          throw new InvalidOperationException();
        
        
        
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
      Udpc.IClient c1, c2;
      Udpc.IServer serv = null;
      if (useUdp)
        serv = TestClient.CreateConnectionUdp(out c1, out c2);
      else
        TestClient.CreateConnectionTest(out c1, out c2, 0);
      ReceiveMessageConversation rcv = null;
      var con1 = new ConversationManager(c1, true);
      //con1.NewConversation = b => new ReceiveMessageConversation(con1);
      var con2 = new ConversationManager(c2, false);

      bool wasReceived1 = false, wasReceived2 = false;
      
      con2.NewConversation = b =>
      {
        rcv = new ReceiveMessageConversation();
        rcv.Completed += (s, e) => wasReceived1 = true; 
        return rcv;
      };


      for (int _i = 0; _i < 3; _i++)
      {
        
        restart:
        rcv = null;
        byte[] bytes = new byte[1000000];
        for (int i = 0; i < bytes.Length; i++)
          bytes[i] = (byte) (i % 3);
        var memstr = new MemoryStream(bytes);
        var snd = new SendMessageConversation(memstr, "TestFile");
        snd.Completed += (s, e) => wasReceived2 = true;

        Thread.Sleep(50);
        con1.StartConversation(snd);
        
        Thread.Sleep(50);
        var sw = Stopwatch.StartNew();
        
        void runProcessing(ConversationManager con, string name)
        {
          while (con.ConversationsActive)
          {
            if(!con.Update())
              Thread.Sleep(1);
          }
        }

        bool done = false;
        void runProcessing2(ConversationManager con, string name)
        {
          while (!done)
          {
            con.Process();
          }
        }
        
        var t3 = Task.Factory.StartNew(() => runProcessing2(con1, "Send_Process"));
        var t4 = Task.Factory.StartNew(() => runProcessing2(con2, "Receive_Process"));
        Thread.Sleep(100);

        var t1 = Task.Factory.StartNew(() => runProcessing(con1, "Send"));
        var t2 = Task.Factory.StartNew(() => runProcessing(con2, "Receive"));
        
        
        t1.Wait();
        t2.Wait();
        done = true;
        t3.Wait();
        t4.Wait();
        if (rcv == null)
          goto restart;
        rcv.Stop();


        var data = File.ReadAllBytes("Downloads/TestFile");
        if (data.Length == 0)
          goto restart;

        if (false == data.SequenceEqual(bytes))
        {
          if (data.Length != bytes.Length)
            throw new InvalidOperationException("Lengths does not match.");
          for (int i = 0; i < data.Length; i++)
          {
            if (data[i] != bytes[i])

              throw new InvalidOperationException("Sequences are not equal");
          }
        }

        Console.WriteLine("Download file done. in {0}ms. {1:F3} MB/s", sw.Elapsed.TotalMilliseconds, 1e-6 * bytes.Length / sw.Elapsed.TotalSeconds);
        if(!(wasReceived1 && wasReceived2))
          throw new InvalidOperationException();
        
      }
      Thread.Sleep(10);
      c1.Disconnect();
      Thread.Sleep(10);
      c2.Disconnect();
      Thread.Sleep(10);
      serv?.Disconnect();
    }

    static void TestFileShare()
    {
      foreach (var x in new[] {"myData", "myData2"})
      {
        try
        {
          Directory.Delete(x, true);
        }
        catch
        {
        
        }

        Directory.CreateDirectory(x);
        foreach (var f in FileShare.TmpFileNames(x))
        {
          try
          {
            Console.WriteLine("delete: {0}", f);
            File.Delete(f);
          }
          catch
          {
            Console.WriteLine("Unable to delete: {0}", f);
          }
        }

      }
      
      Console.Out.Flush();
      Thread.Sleep(200);
      
      
      var fs = FileShare.Create("test2@0.0.0.0:share1", "myData");
      Thread.Sleep(200);
      var fs2 = FileShare.Create("test3@0.0.0.0:share1", "myData2");
      Thread.Sleep(200);
      fs.ConnectTo(fs2.Service);
      Thread.Sleep(200);
      //fs2.ConnectTo(fs.Service);
      
      void updateFileShares()
      {
        while (!fs.ShutdownPending && !fs2.ShutdownPending)
        {
          fs.UpdateIfNeeded();
          fs.WaitForProcessing();
          Thread.Sleep(100);
          fs2.UpdateIfNeeded();
          fs2.WaitForProcessing();
          Thread.Sleep(100);
        }
      }
      
      var trd = new Thread(updateFileShares);
      
      
      StringBuilder textdata = new StringBuilder();
      
      
      void iterate(string dir)
      {
        var filename = Guid.NewGuid().ToString();
        textdata.Append(filename);
        File.WriteAllText(Path.Combine(dir,filename), textdata.ToString());  
      }
      
      iterate(fs.DataFolder);
      iterate(fs2.DataFolder);
      trd.Start();
      Thread.Sleep(100);
      for (int i = 0; i < 10; i++)
      {
        iterate(i%2 == 0 ? fs.DataFolder : fs2.DataFolder);
        Thread.Sleep(50);
      }

      bool condition()
      {
        var d1 = Directory.GetFiles(fs.DataFolder);
        var d2 = Directory.GetFiles(fs2.DataFolder);
        return d1.Length == d2.Length;
      }

      var sw = Stopwatch.StartNew();
      while (condition() == false)
      {
        //if(sw.ElapsedMilliseconds > 20000)
        //  throw new InvalidOperationException("test timed out");
        Thread.Sleep(100);
      }
      Thread.Sleep(100);

      GC.Collect();  
      var chunkCount = Directory.GetFiles(".").Count(x => x.Contains(".gitChunk"));
      while (chunkCount > 0)
      {
        chunkCount = Directory.GetFiles(".").Count(x => x.Contains(".gitChunk"));
        Thread.Sleep(100);
        //throw new InvalidOperationException("missing clean up");
      }

      fs.Stop();
      fs2.Stop();
      trd.Join();
      fs.WaitForShutdown();
      fs2.WaitForShutdown();
    }


    public void TestDataLog()
    {
      string datafile = "/tmp/rolf/datalog/datafile.bin";
      try
      {
        File.Delete(datafile);
      }
      catch
      {
        
      }
      
      string commits = "/tmp/rolf/datalog/datafile.bin.commits";
      try
      {
        File.Delete(commits);
      }
      catch
      {
      }

      string datafile2 = "/tmp/rolf/datalog/datafile.2.bin";
      try
      {
        File.Delete(datafile2);
      }
      catch
      {
        
      }
      
      string commits2 = "/tmp/rolf/datalog/datafile.bin.2.commits";
      try
      {
        File.Delete(commits2);
      }
      catch
      {

      }
      
      try
      {  
        Directory.Delete("Downloads2", true);

      }
      catch
      {
        
      }
      
      try
      {  
        Directory.Delete("Downloads", true);

      }
      catch
      {
        
      }
      
      
      System.Reflection.Assembly.LoadFrom("Blake2Sharp.dll");
      var dl1 = new DataLog.DataLog("Downloads", datafile, commits);
      dl1.Update();
      
      dl1.Update();
      dl1.Flush();
      
      var dl2 = new DataLog.DataLog("Downloads2", datafile2, commits2);
      dl2.Update();
      
      //File.WriteAllText("Downloads2/testtest", "kijdwoahdopwaj;dwah;sda;hdio;wasd");
      File.WriteAllText("Downloads/testtest.3124.jwidah", "kijdwoahdopwaj;dwah;sda;hdio;wasd");
      Directory.CreateDirectory("Downloads/testdir");
      
      dl2.Update();
      dl1.Update();
      
      dl2.Unpack(DataLogCore.ReadFromFile(datafile));
      for (int i = 0; i < 6; i++)
      {
        string file;
        if (i % 2 == 1)
          file = "Downloads/test.1.bin";
        else
          file = "Downloads2/test.2.bin";

        File.WriteAllText(file, "asdasd" + new string('x', i));
        
        
        var p11_hashes1 = dl1.LogCore.ReadCommitHashes().ToArray();
        var p11_hashes2 = dl2.LogCore.ReadCommitHashes().ToArray();
        if(p11_hashes1.Length != dl1.LogCore.CommitsCount)
          throw new Exception();
        if(p11_hashes2.Length != dl2.LogCore.CommitsCount)
          throw new Exception();
        dl2.Update();
        dl1.Update();
        var p12_hashes1 = dl1.LogCore.ReadCommitHashes().ToArray();
        var p12_hashes2 = dl2.LogCore.ReadCommitHashes().ToArray();
        
        if(p12_hashes1.Length <= p11_hashes1.Length && (i%2 == 1))
          throw new Exception();
        if(p12_hashes2.Length <= p11_hashes2.Length && (i%2 == 0))
          throw new Exception();
        if(p12_hashes1.Length > p11_hashes1.Length && (i%2 == 0))
          throw new Exception();
        if(p12_hashes2.Length > p11_hashes2.Length && (i%2 == 1))
          throw new Exception();
        
        if (i == 3) 
          continue;
        
        DataLogMerge.MergeDataLogs(dl2, dl1, 1000);
        DataLogMerge.MergeDataLogs(dl1, dl2, 1000);
        var hashes1 = dl1.LogCore.ReadCommitHashes().ToArray();
        var hashes2 = dl2.LogCore.ReadCommitHashes().ToArray();
        
        for (int j = 0; j < hashes1.Length; j++)
        {

          if (hashes1[j].Equals(hashes2[j]) == false)
          {
            throw new InvalidOperationException();
          }
        }
        if(hashes1.SequenceEqual(hashes2) == false)
          throw new InvalidOperationException();
      }

      dl2.Dispose();
      dl1.Dispose();
    }

    public void DataLogStream()
    {
      var hsh = new DataLogHash { A = 5, B = 6, C = 9, D = 100, Length = 1000};
      using (var memstr = new MemoryStream())
      {
        hsh.ToStream(memstr);
        memstr.Position = 0;
        var hsh2 = DataLogHash.Read(memstr);
        if(hsh.Equals(hsh2) == false)
          throw new InvalidOperationException();
      }
    }

    public void TestAppendStream()
    {
      byte[] data1 = {1, 2, 3, 4};
      byte[] data2 = {5,6, 7, 8};
      byte[] data3 = {};
      byte[] data4 = {9,10};
      var combined = new[] {data1, data2, data3, data4};
      var ccat = combined.SelectMany(x => x).ToArray();
      var streams = combined.Select(x =>(Stream) new MemoryStream(x));
      using (var app = new CombinedStreams(streams.ToArray()))
      {
        var bytes = app.ReadBytes(9);
        if(bytes.SequenceEqual(ccat.Take(9)) == false)
          throw new InvalidOperationException();
        bytes = app.ReadBytes(1);
        if(bytes.SequenceEqual(ccat.Skip(9)) == false)
          throw new InvalidOperationException();
        app.Seek(4, SeekOrigin.Begin);
        bytes = app.ReadBytes(4);
        if(bytes.SequenceEqual(ccat.Skip(4).Take(4)) == false)
          throw new InvalidOperationException();
      }
    }
    

    public void RunTests()
    {
      DataLogStream();
      TestAppendStream();
      
      for(int i = 0; i < 5; i++)
        TestDataLog();
      
      //for (int i = 0; i < 3; i++)
      //  w  TestFileConversation(false);

      TestCircularSum();
      TestUtils();

      var trd = new Thread(runServer) {IsBackground = true};
      trd.Start();

      Thread.Sleep(200);
      for (int i = 0; i < 100; i++)
      {
        
        Console.WriteLine("Testing {0}", i);
        Thread.Sleep(100);
        TestFileShare();
        Console.WriteLine("Finished with {0}", i);
      }

      return;
      UdpcLatencyTest();
      UdpcBasicInterop();

      UdpcAbstractTest();
      ConversationTest();

      var sw2 = Stopwatch.StartNew();
      for (int i = 0; i < 20; i++)
      {
        Console.WriteLine(">> {0}", i);
        TestFileConversation(true);
      }

      Console.WriteLine("Time spent: {0}", sw2.Elapsed);

      TestFileConversation(true);
      Console.WriteLine("Tests finished");
    }

  }
}