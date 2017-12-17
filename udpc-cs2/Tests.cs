using System;
using System.IO;
using System.Threading.Tasks;

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
      Console.WriteLine("Hello World!");
      Console.WriteLine("Hello world2");
      Console.WriteLine("Hello world3!!");
      int x = 5;
      int y = 425;
      Console.WriteLine("??? {0}", x + y);
      UdpcApi.udpc_net_load();
      IntPtr con = UdpcApi.udpc_login("rolf@0.0.0.0:test1");
      var tsk = Task.Factory.StartNew(() => {
        IntPtr c2 = UdpcApi.udpc_connect("rolf@0.0.0.0:test1");
        UdpcApi.udpc_write(c2, new byte[]{1,2,3}, 3);
        UdpcApi.udpc_close(c2);
      });

      IntPtr c = UdpcApi.udpc_listen(con);
      byte[] testbytes = new byte[3];

      UdpcApi.udpc_read(c, testbytes, (ulong)testbytes.LongLength);
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
        cli.Write(new byte[]{1,2,3}, 3);
        cli.Disconnect();
      });
      var s1 = serv.Listen();
      byte[] testbytes = new byte[3];

      s1.Read(testbytes, testbytes.Length);
      tsk.Wait();
      Console.WriteLine("Got bytes {0} {1} {2}. {3}", testbytes[0], testbytes[1], testbytes[2],DateTime.Now);
      s1.Disconnect();
      serv.Disconnect();
      Console.WriteLine("Finished!");
    }

    public void RunTests()
    {
      //GitInterop();
      //UdpcBasicInterop();
      UdpcSendFile();
    }

  }
}