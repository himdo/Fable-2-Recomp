# Per-thread CPU use of a running fable_2.exe over a sampling window, by
# thread name (the SDK names its threads: "GPU Commands" = the Xenos command
# processor, "Main XThread" / guest XThreads = recompiled game code, ...).
#
#   powershell -File tools\thread_cpu.ps1 [-ProcessId N] [-Seconds 30] [-Top 15]
#
# 100% = one fully busy core. Shows whether frame time goes to GPU emulation
# or to the game's own (recompiled) threads.
param([int]$ProcessId = 0, [int]$Seconds = 30, [int]$Top = 15)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ThreadNames {
  [DllImport("kernel32.dll")] static extern IntPtr OpenThread(uint access, bool inherit, uint id);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr thread, out IntPtr description);
  [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
  public static string Get(int id) {
    IntPtr thread = OpenThread(0x0800 /* QUERY_LIMITED_INFORMATION */, false, (uint)id);
    if (thread == IntPtr.Zero) return "";
    try {
      IntPtr text;
      if (GetThreadDescription(thread, out text) < 0 || text == IntPtr.Zero) return "";
      string name = Marshal.PtrToStringUni(text);
      LocalFree(text);
      return name;
    } finally { CloseHandle(thread); }
  }
}
"@

if ($ProcessId -eq 0) { $ProcessId = (Get-Process fable_2 -ErrorAction Stop | Select-Object -First 1).Id }
function Snapshot {
  $times = @{}
  foreach ($t in (Get-Process -Id $ProcessId).Threads) {
    try { $times[$t.Id] = $t.TotalProcessorTime.TotalMilliseconds } catch {}
  }
  $times
}
$before = Snapshot
$start = Get-Date
Start-Sleep -Seconds $Seconds
$after = Snapshot
$elapsedMs = ((Get-Date) - $start).TotalMilliseconds

$rows = foreach ($id in $after.Keys) {
  $base = if ($before.ContainsKey($id)) { $before[$id] } else { 0 }
  $name = [ThreadNames]::Get($id)
  [pscustomobject]@{ Thread = $id; Name = $(if ($name) { $name } else { '(unnamed)' });
                     CpuPct = [math]::Round(100 * ($after[$id] - $base) / $elapsedMs, 1) }
}
$rows | Sort-Object CpuPct -Descending | Select-Object -First $Top | Format-Table -AutoSize | Out-String -Width 120
"{0} threads, total {1:N0}% of one core over {2:N0} s" -f $rows.Count, (($rows | Measure-Object CpuPct -Sum).Sum), ($elapsedMs / 1000)
