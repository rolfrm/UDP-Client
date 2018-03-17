﻿using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.IO;
using System.Text;

namespace Udpc.Share
{
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
      Name = name;
      Status = status;
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

  public struct GitBranchDifference
  {
    public string CommonBase;
    public string ATop;
    public string BTop;
  }

  public interface ISyncObject
  {
    
  }

  public interface IFileShare
  {
    void Init(string Directory);
    void Commit();
    object GetSyncData();
    object GetSyncDiff(object remoteSync);
    Stream OpenSyncStream(object remoteHashes);
    void CloseSyncStream(Stream stream);
    void UnpackSyncStream(Stream syncStream);
  }

  [Serializable]
  public class GitSync : ISyncObject
  {
    public List<string> Log;
    public string BaseCommit;

  }
  
  public class Git : IFileShare
  {
    public string DirPath { get; private set; }
    public bool IsBare = false;

    public void Init(string directory)
    {
      if(DirPath != null) throw new InvalidOperationException();
      DirPath = directory;
      var dir = new DirectoryInfo(Path.Combine(DirPath, ".git"));
      if (!dir.Exists)
      {
        if (IsBare)
        {
          runProcess("git", "--bare", "init");
        }

        else
        {
          runProcess("git", "init");
        }
      }
    }

    public void Commit()
    {
      CommitAll();
    }

    public object GetSyncData()
    {
      return new GitSync() {Log = GetLog()};
    }

    public object GetSyncDiff(object _remoteSync)
    {
      var remoteSync = (GitSync) _remoteSync;
      var sync = (GitSync)GetSyncData();
      var diff = GetCommonBase(sync.Log, remoteSync.Log);
      return new GitSync() {BaseCommit = diff};
    }

    public Stream OpenSyncStream(object remoteHashes)
    {
      var syncdata = (GitSync) remoteHashes;
      var file = GetPatch(syncdata.BaseCommit);
      var fstr = File.OpenRead(file);
      return new EventWrappedStream(fstr)
      {
        OnClosed = () => File.Delete(file)
      };
    }

    public void CloseSyncStream(Stream stream)
    {
      throw new NotImplementedException();
    }

    public void UnpackSyncStream(Stream syncStream)
    {
      var fname1 = Guid.NewGuid().ToString();
      var filename = Path.Combine(DirPath, fname1);
      using (var f = File.OpenWrite(filename))
      {
        syncStream.CopyTo(f);
        syncStream.Flush();
      }
      syncStream.Close();
      
      ApplyPatch(fname1);
      File.Delete(filename);
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
        
        
        if(!newitem.Name.StartsWith('.'))
          gitstatus.Items.Add(newitem);
      }

      return gitstatus;
    }

    public List<string> GetLog()
    {
      var result = runProcess(true, "git", "log", "--full-history", "--oneline", "--no-abbrev-commit");
      if (result.ExitCode != 0)
        return new List<string>();
      var logs = result.Output.Split("\n").Select(x => x.Trim()).ToList();
      return logs.Select(x => x.Split(" ", StringSplitOptions.RemoveEmptyEntries).FirstOrDefault())
        .Where(x => x != null)
        .ToList();
    }


    int commits;

    public bool CommitAll()
    {
      var commits1 = commits;
      while (true)
      {
        var status = GetGitStatus();
        if (status.Items.Count == 0)
          break;

        foreach (var item in status.Items)
        {
          runProcess("git", "add", string.Format("\"{0}\"", item.Name));
        }
        runProcess("git", "commit", "-m", string.Format("\"{0},{1}\"", DirPath, ++commits));
      }

      return commits1 != commits;
    }

    public string GetPatch(string baseCommit = null)
    {
      var fname = $".patch.{Guid.NewGuid()}.bin";
      if (baseCommit != null)
        runProcess("git", "bundle", "create", fname, "master", $"^{baseCommit}");
      else
        runProcess("git", "bundle", "create", fname, "master");

      return Path.GetFullPath(Path.Combine(DirPath, fname));
    }

    public void ApplyPatch(string patchfile)
    {
      patchfile = $"\"{patchfile}\"";
      runProcess("git", "fetch", patchfile, "master:patch");
      runProcess("git", "merge", "patch");
      runProcess("git", "branch", "-D", "patch");
      runProcess("rm", patchfile);
    }

    static public string GetCommonBase(List<string> log2, List<string> log3)
    {
      string match = null;
      var search = Math.Min(log2.Count, log3.Count);
      for (int i = 0; i < search; i++)
      {
        var l1 = log2[log2.Count - 1 - i];
        var l2 = log3[log3.Count - 1 - i];
        if (l1 != l2) break;
        match = log2[log2.Count - 1 - i];
      }
      return match;
    }
    
    static public string GetCommonBase(Git gitA, Git gitB)
    {
      var log2 = gitA.GetLog();
      var log3 = gitB.GetLog();
      string match = null;
      var search = Math.Min(log2.Count, log3.Count);
      for (int i = 0; i < search; i++)
      {
        var l1 = log2[log2.Count - 1 - i];
        var l2 = log3[log3.Count - 1 - i];
        if (l1 != l2) break;
        match = log2[log2.Count - 1 - i];
      }


      return match;
    }

    public string SyncPatch()
    {
      runProcess("git", "commit", "--allow-empty", "-m", "sync");
      runProcess("git", "bundle", "create", ".patch.bin", "HEAD", "--max-count=2");
      return Path.GetFullPath(Path.Combine(DirPath, ".patch.bin"));
    }

    ProcStatus runProcess(string name, params string[] args)
    {
      return runProcess(false, name, args);
    }

    ProcStatus runProcess(bool ignoreErrors, string name, params string[] args)
    {
      //Console.WriteLine("Running: '{0} {1}'", name, string.Join(" ", args));
      var startinfo = new ProcessStartInfo(name, string.Join(" ", args))
      {
        WorkingDirectory = DirPath,
        CreateNoWindow = true,
        RedirectStandardError = true,
        RedirectStandardOutput = true,
        UseShellExecute = false,

      };

      using (var proc = new Process(){StartInfo =  startinfo})
      {
        var output = new StringBuilder();
        var erroroutput = new StringBuilder();
        proc.ErrorDataReceived += (sender, evt) => erroroutput.AppendLine(evt.Data);
        proc.OutputDataReceived += (sender, evt) => output.AppendLine(evt.Data);

        proc.Start();
        proc.BeginErrorReadLine();
        proc.BeginOutputReadLine();
        proc.WaitForExit();

        //Console.WriteLine("{0}", output.ToString());

        if(proc.ExitCode != 0 && !ignoreErrors)
          throw new InvalidOperationException(string.Format("program exited with error '{0}'", erroroutput.ToString()));

        return new ProcStatus()
        {
          Output = output.ToString(),
          ErrorOutput = erroroutput.ToString(),
          ExitCode = proc.ExitCode
        };
      }
    }

  }
}