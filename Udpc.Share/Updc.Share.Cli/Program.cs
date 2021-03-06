﻿using System;
using System.Threading;
using Udpc.Share;

namespace Updc.Share.Cli
{
    class Program
    {
        static void Main(string[] args)
        {
            string user = args[0];
            string path = args[1];
            string share = args.Length > 2 ? args[2] : null;
            
            Console.WriteLine("User: {0}, path: {1}, share: {2}", user, path, share ?? "NULL");
            
            
            System.IO.Directory.CreateDirectory(path);
            
            var fs = FileShare.Create(user, path);
            if(share != null)
                fs.ConnectTo(share);

            while (true)
            {
                Thread.Sleep(500);
                fs.UpdateIfNeeded();
            }
        }
    }
}