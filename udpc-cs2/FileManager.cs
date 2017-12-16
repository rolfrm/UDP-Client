using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;

namespace udpc_cs2
{
    /// <summary>
    /// This class uses git to have a version controlled list of files.
    /// </summary>
    public class FileManager
    {
        public readonly string Path;

        FileManager(string path)
        {
            this.Path = path;
        }

        public static FileManager Create(string path)
        {
            return new FileManager(path);
        }

        public void Update()
        {
        }
    }


    public enum GitItemStatus
    {
        Untracked,
        Deleted,
        Modified
    }

    public class GitItem
    {
        public GitItem(string name, GitItemStatus status)
        {
            this.Name = name;
            this.Status = status;
        }

        public readonly string Name;

        public readonly GitItemStatus Status;
    }

    public class GitStatus
    {
        public List<GitItem> Items { get; private set; }

        public GitStatus()
        {
            Items = new List<GitItem>();
        }

    }

    public struct ProcStatus
    {
        public string Output;
        public int ExitCode;
        public string ErrorOutput;
    }

    public class Git
    {
        public readonly string DirPath;

        public Git(string dirPath)
        {
            DirPath = dirPath;
        }

        public void Init()
        {
            var dir = new DirectoryInfo(Path.Combine(DirPath, ".git"));
            if (!dir.Exists)
            {
                runProcess("git", "init");
            }
        }

        public GitStatus GetGitStatus()
        {
            var result = runProcess("git", "status", "--short", "--no-column");
            var gitstatus = new GitStatus();
            if(result.ExitCode != 0)
                throw new InvalidOperationException("error while running git.");
            var things = result.Output.Split("\n", StringSplitOptions.RemoveEmptyEntries);
            foreach (var firstthing in things)
            {
                var thing = firstthing.Trim();
                var idx = thing.IndexOf(' ');
                if (idx == -1) continue;
                var fst = thing.Substring(0, idx);
                var snd = thing.Substring(idx + 1);
                if (snd.FirstOrDefault() == '"' && snd.LastOrDefault() == '"' && snd.Length > 1)
                    snd = snd.Substring(1, snd.Length - 2);

                var newitem = new GitItem(snd, GitItemStatus.Untracked);
                if(fst.Contains("M"))
                    newitem = new GitItem(snd, GitItemStatus.Modified);
                else if(fst.Contains("D"))
                    newitem = new GitItem(snd, GitItemStatus.Deleted);

                gitstatus.Items.Add(newitem);
            }

            return gitstatus;
        }

        public List<string> GetLog()
        {
            try
            {
                var result = runProcess("git", "log", "--full-history", "--oneline", "--no-abbrev-commit");
                var logs = result.Output.Split("\n").Select(x => x.Trim()).ToList();
                return logs.Select(x => x.Split(" ", StringSplitOptions.RemoveEmptyEntries).FirstOrDefault())
                    .Where(x => x != null)
                    .ToList();
            }
            catch (Exception e) when(e.Message.Contains("does not have any commits yet"))
            {

                return new List<string>();
            }
        }

        public void CommitAll()
        {
            while (true)
            {
                var status = GetGitStatus();
                if (status.Items.Count == 0)
                    break;

                foreach (var item in status.Items)
                {
                    runProcess("git", "add", string.Format("\"{0}\"", item.Name));
                }
                runProcess("git", "commit", "-m", "\"x\"");
            }
        }

        ProcStatus runProcess(string name, params string[] args)
        {
            Console.WriteLine("Running: '{0} {1}'", name, string.Join(" ", args));
            var startinfo = new ProcessStartInfo(name, string.Join(" ", args))
            {
                WorkingDirectory = DirPath,
                CreateNoWindow = true,
                RedirectStandardError = true,
                RedirectStandardOutput = true,
                UseShellExecute = false
            };

            using (var proc = new System.Diagnostics.Process(){StartInfo =  startinfo})
            {
                var output = new StringBuilder();
                var erroroutput = new StringBuilder();
                proc.ErrorDataReceived += (sender, evt) => erroroutput.AppendLine(evt.Data);
                proc.OutputDataReceived += (sender, evt) => output.AppendLine(evt.Data);

                proc.Start();
                proc.BeginErrorReadLine();
                proc.BeginOutputReadLine();
                proc.WaitForExit();

                Console.WriteLine("{0}", output.ToString());

                if(proc.ExitCode != 0)
                    throw new InvalidOperationException(string.Format("program exited with error '{0}'", erroroutput.ToString()));

                return new ProcStatus()
                {
                    Output = output.ToString(),
                    ErrorOutput = erroroutput.ToString(),
                    ExitCode = proc.ExitCode
                };
            }
        }

        public string GetPatch(string first, string baseCommit = null)
        {
            if (baseCommit != null)
                runProcess("git", "bundle", "create", ".patch.bin", "master", $"^{baseCommit}");
            else
                runProcess("git", "bundle", "create", ".patch.bin", "master");



            return Path.GetFullPath(Path.Combine(DirPath, ".patch.bin"));
        }

        public void ApplyPatch(string patchfile)
        {
            patchfile = $"\"{patchfile}\"";
            runProcess("git", "fetch", patchfile, "master:patch");
            runProcess("git", "merge", "patch");
            runProcess("git", "branch", "-D", "patch");
            runProcess("rm", patchfile);
        }
    }

    public class ProcessRunner
    {

    }

}