using System.Linq;
namespace udpc_cs2
{
    static class Program
    {
        static void Main(string[] args)
        {
            if (args.Contains("--test"))
            {
                var tst = new Tests();
                tst.RunTests();
                return;
            }

            
            
        }
    }
}
