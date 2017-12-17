using System;
using System.IO;
using System.Linq;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
namespace udpc_cs2
{
    class Program
    {
        static void Main(string[] args)
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

            return;

            Console.WriteLine("Hello World!");
            Console.WriteLine("Hello world2");
            Console.WriteLine("Hello world3!!");
            int x = 5;
            int y = 425;
            Console.WriteLine("??? {0}", x + y);
            Udpc.udpc_net_load();
            IntPtr con = Udpc.udpc_login("rolf@0.0.0.0:test1");
            var tsk = Task.Factory.StartNew(() => {
                IntPtr c2 = Udpc.udpc_connect("rolf@0.0.0.0:test1");
                Udpc.udpc_write(c2, new byte[]{1,2,3}, 3);
                Udpc.udpc_close(c2);
            });

            IntPtr c = Udpc.udpc_listen(con);
            byte[] testbytes = new byte[3];

            Udpc.udpc_read(c, testbytes, (ulong)testbytes.LongLength);
            tsk.Wait();
            Console.WriteLine("Got bytes {0} {1} {2}. {3}", testbytes[0], testbytes[1], testbytes[2],DateTime.Now);
            Udpc.udpc_logout(con);
            Console.WriteLine("Exit..");

        }
    }

    class Udpc {
        public void Connect(){

        }

        static Udpc()
        {
            dlopen("./libudpc_net.so", RTLD_NOW + RTLD_GLOBAL);
        }
        [DllImport("libdl.so")]
        protected static extern IntPtr dlopen(string filename, int flags);

        [DllImport("libdl.so")]
        protected static extern IntPtr dlsym(IntPtr handle, string symbol);

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
        public static extern void udpc_read(IntPtr con, byte[] buffer, ulong size);

        [DllImport("libudpc.so")]
        public static extern int udpc_peek(IntPtr con, byte[] buffer, ulong size);

    }

}
