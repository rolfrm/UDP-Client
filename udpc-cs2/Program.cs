using System.IO;
using System.Linq;
using System.Runtime.ExceptionServices;
using System.Threading.Tasks;
namespace udpc_cs2
{
    class Program
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
