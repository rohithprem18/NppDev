# Recent Folders picker for Notepad++ (VS Code style).
# Lists folders from: folders opened through this picker, Notepad++'s saved
# "Folder as Workspace" roots, and the folders of Notepad++'s recent files.
# Opening a folder adds it to the Folder as Workspace panel.
#   -ListOnly        print the folder list (and save it, so folders opened with
#                    File > Open Folder as Workspace are remembered too)
#   -Open <folder>   open that folder without showing the window (in place when the plugin is loaded)
#   -NppPid <id>     the Notepad++ window to reopen with the folder (several windows can be open);
#                    without it, the Notepad++ window that was in front is used
# The RecentFolders plugin (File > Open Recent Folder) reads recent-folders-cache.txt.
param([switch]$ListOnly, [string]$Open, [int]$NppPid = 0)

# Windows API helpers. Compiling them takes about half a second, so they are compiled once into
# recent-folders-win.dll (again whenever this script changes) and only loaded when needed.
$win32Source = @'
using System; using System.Text; using System.Runtime.InteropServices;
namespace RecentFolders { public class Win {
public delegate bool EnumProc(System.IntPtr h, System.IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, System.IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(System.IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[StructLayout(LayoutKind.Sequential)] public struct CopyData { public System.IntPtr dwData; public int cbData; public System.IntPtr lpData; }
[DllImport("user32.dll")] static extern System.IntPtr SendMessageTimeout(System.IntPtr h, uint m, System.IntPtr w, ref CopyData l, uint flags, uint ms, out System.IntPtr result);
// Ask a Notepad++ window (with the RecentFolders plugin) to switch its workspace to a folder in place.
public static bool SendFolder(System.IntPtr h, string folder) {
    CopyData cd = new CopyData(); cd.dwData = (System.IntPtr)0x4E505046; cd.cbData = folder.Length * 2;
    cd.lpData = Marshal.StringToHGlobalUni(folder);
    try { System.IntPtr r; return SendMessageTimeout(h, 0x004A, System.IntPtr.Zero, ref cd, 2, 15000, out r) != System.IntPtr.Zero && r != System.IntPtr.Zero; }
    finally { Marshal.FreeHGlobal(cd.lpData); } }
// The "Notepad++" class window of a process (not a floating panel).
public static System.IntPtr FindNpp(uint procId) {
    System.IntPtr found = System.IntPtr.Zero;
    EnumWindows((h, l) => {
        uint p; GetWindowThreadProcessId(h, out p);
        if (p != procId) return true;
        StringBuilder c = new StringBuilder(64); GetClassName(h, c, 64);
        if (c.ToString() != "Notepad++") return true;
        found = h; return false; }, System.IntPtr.Zero);
    return found; }
} }
'@
function Use-Win32 {
    if ('RecentFolders.Win' -as [type]) { return }
    $dll = Join-Path $PSScriptRoot 'recent-folders-win.dll'
    if (-not (Test-Path $dll) -or (Get-Item $dll).LastWriteTime -lt (Get-Item $PSCommandPath).LastWriteTime) {
        try { Add-Type -TypeDefinition $win32Source -OutputAssembly $dll -OutputType Library -ErrorAction Stop } catch { }
    }
    if (-not ('RecentFolders.Win' -as [type])) {
        try { Add-Type -Path $dll -ErrorAction Stop } catch { Add-Type -TypeDefinition $win32Source }
    }
    if (-not $script:NppPid) {          # launched from the Run menu: the Notepad++ window in front
        $fp = [uint32]0; [void][RecentFolders.Win]::GetWindowThreadProcessId([RecentFolders.Win]::GetForegroundWindow(), [ref]$fp)
        if ((Get-Process -Id $fp -ErrorAction SilentlyContinue).ProcessName -eq 'notepad++') { $script:NppPid = [int]$fp }
    }
}
# Close one Notepad++ window by process id (its "Notepad++" class window, not a floating panel).
function Close-NppWindow([int]$procId) {
    $h = [RecentFolders.Win]::FindNpp([uint32]$procId)
    if ($h -ne [IntPtr]::Zero) { [void][RecentFolders.Win]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
}

$ErrorActionPreference = 'SilentlyContinue'
# NppDev keeps this script in <app>\devtools next to a portable notepad++.exe (settings in <app>);
# a hand-made setup uses the installed Notepad++ and %APPDATA%\Notepad++.
$npp = Join-Path (Split-Path $PSScriptRoot) 'notepad++.exe'
if (-not (Test-Path -LiteralPath $npp)) { $npp = 'C:\Program Files\Notepad++\notepad++.exe' }
$config = if (Test-Path -LiteralPath (Join-Path (Split-Path $npp) 'doLocalConf.xml')) { Join-Path (Split-Path $npp) 'config.xml' }
          else { Join-Path $env:APPDATA 'Notepad++\config.xml' }
$mruFile = Join-Path $PSScriptRoot 'recent-folders.txt'
$hiddenFile = Join-Path $PSScriptRoot 'recent-folders-hidden.txt'   # folders removed from the list
$cacheFile = Join-Path $PSScriptRoot 'recent-folders-cache.txt'     # UTF-16 list for the plugin
$maxMru  = 25

function Get-Folders {
    $list = New-Object System.Collections.Generic.List[string]
    $x = $null; if (Test-Path $config) { $x = [xml](Get-Content $config -Raw) }
    # The list itself first (the plugin keeps it in most-recent order), then the workspace folder(s)
    # saved at Notepad++'s last exit, then the folders of recent files
    if (Test-Path $mruFile) { Get-Content $mruFile | Where-Object { $_.Trim() } | ForEach-Object { $list.Add($_.Trim()) } }
    if ($x) { $x.NotepadPlus.FileBrowser.root | ForEach-Object { if ($_.foldername) { $list.Add($_.foldername) } } }
    if ($x) { $x.NotepadPlus.History.File | ForEach-Object { if ($_.filename) { $list.Add((Split-Path $_.filename -Parent)) } } }
    $seen = @{}
    if (Test-Path $hiddenFile) { Get-Content $hiddenFile | Where-Object { $_.Trim() } | ForEach-Object { $seen[$_.Trim().ToLower()] = $true } }
    $list | ForEach-Object { $_.TrimEnd('\') } |
        Where-Object { $_ -and -not $seen.ContainsKey($_.ToLower()) -and (Test-Path -LiteralPath $_ -PathType Container) } |
        ForEach-Object { $seen[$_.ToLower()] = $true; $_ }
}

function Save-Mru([string[]]$folders) {
    $folders = @($folders | Select-Object -First $maxMru)
    $folders | Set-Content -LiteralPath $mruFile -Encoding UTF8
    $folders | Set-Content -LiteralPath $cacheFile -Encoding Unicode
}

function Remember-Folder([string]$path) {
    # Re-opening a folder un-hides it
    if (Test-Path $hiddenFile) { (Get-Content $hiddenFile | Where-Object { $_.Trim().ToLower() -ne $path.TrimEnd('\').ToLower() }) | Set-Content -LiteralPath $hiddenFile -Encoding UTF8 }
    $rest = Get-Folders | Where-Object { $_ -ne $path.TrimEnd('\') }
    Save-Mru (@($path.TrimEnd('\')) + @($rest))
}

function Open-Folder([string]$path) {
    $target = $null
    if ($NppPid) { $target = Get-Process -Id $NppPid -ErrorAction SilentlyContinue }
    if (-not $target) { $target = Get-Process notepad++ -ErrorAction SilentlyContinue | Select-Object -First 1 }
    # Fast path: the plugin switches the workspace inside the running window (and updates the recent list)
    if ($target) {
        $h = [RecentFolders.Win]::FindNpp([uint32]$target.Id)
        if ($h -ne [IntPtr]::Zero -and [RecentFolders.Win]::SendFolder($h, $path.TrimEnd('\'))) { return }
    }
    Remember-Folder $path
    # Fallback: a running Notepad++ ignores -openFoldersAsWorkspace, so reopen that window with the folder
    # (like VS Code switching folders). Other Notepad++ windows are left alone.
    # Unsaved files survive because session snapshot/backup is enabled.
    $wasExtraWindow = $false
    if ($target) {
        $wasExtraWindow = (Get-CimInstance Win32_Process -Filter "ProcessId=$($target.Id)").CommandLine -match '-nosession'
        Close-NppWindow $target.Id
        $deadline = (Get-Date).AddSeconds(60)
        while ((Get-Process -Id $target.Id -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 300 }
        if (Get-Process -Id $target.Id -ErrorAction SilentlyContinue) {
            [void][System.Windows.Forms.MessageBox]::Show('Notepad++ did not close (is it asking something?). Close it, then pick the folder again.', 'Recent Folders')
            return
        }
    }
    $argList = @('-openFoldersAsWorkspace', "`"$($path.TrimEnd('\'))`"")
    # with another window still open, Notepad++ would hand the folder to that one: start a separate window
    if (Get-Process notepad++ -ErrorAction SilentlyContinue) { $argList = @('-multiInst') + $argList }
    if ($wasExtraWindow) { $argList = @('-nosession') + $argList }          # an extra window stays an extra window
    Start-Process -FilePath $npp -ArgumentList $argList
}

if ($ListOnly) { $all = @(Get-Folders); Save-Mru $all; $all; return }
Use-Win32

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
if ($Open) { Open-Folder $Open; return }
[System.Windows.Forms.Application]::EnableVisualStyles()

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Recent Folders - Notepad++'
$form.Size = New-Object System.Drawing.Size(640, 440)
$form.StartPosition = 'CenterScreen'
$form.KeyPreview = $true
$form.TopMost = $true
$form.Font = New-Object System.Drawing.Font('Segoe UI', 10)

$hint = New-Object System.Windows.Forms.Label
$hint.Text = 'Double-click or press Enter to open.  Esc to close.  Del to remove from list.'
$hint.Dock = 'Top'; $hint.Height = 26; $hint.Padding = New-Object System.Windows.Forms.Padding(6, 6, 6, 0)

$listBox = New-Object System.Windows.Forms.ListBox
$listBox.Dock = 'Fill'
$listBox.IntegralHeight = $false
$listBox.DrawMode = 'OwnerDrawFixed'
$listBox.ItemHeight = 42
$nameFont = New-Object System.Drawing.Font('Segoe UI', 10, [System.Drawing.FontStyle]::Bold)
$pathFont = New-Object System.Drawing.Font('Segoe UI', 8.5)
$listBox.add_DrawItem({
    param($s, $e)
    if ($e.Index -lt 0) { return }
    $e.DrawBackground()
    $p = [string]$listBox.Items[$e.Index]
    $name = Split-Path $p -Leaf; if (-not $name) { $name = $p }
    $selected = ($e.State -band [System.Windows.Forms.DrawItemState]::Selected) -ne 0
    $c1 = if ($selected) { [System.Drawing.SystemColors]::HighlightText } else { [System.Drawing.SystemColors]::WindowText }
    $c2 = if ($selected) { [System.Drawing.SystemColors]::HighlightText } else { [System.Drawing.SystemColors]::GrayText }
    [System.Windows.Forms.TextRenderer]::DrawText($e.Graphics, $name, $nameFont, (New-Object System.Drawing.Point(($e.Bounds.X + 8), ($e.Bounds.Y + 3))), $c1)
    [System.Windows.Forms.TextRenderer]::DrawText($e.Graphics, $p, $pathFont, (New-Object System.Drawing.Point(($e.Bounds.X + 8), ($e.Bounds.Y + 22))), $c2)
    $e.DrawFocusRectangle()
})

$buttons = New-Object System.Windows.Forms.FlowLayoutPanel
$buttons.Dock = 'Bottom'; $buttons.Height = 44; $buttons.FlowDirection = 'RightToLeft'
$buttons.Padding = New-Object System.Windows.Forms.Padding(6)
$btnOpen   = New-Object System.Windows.Forms.Button; $btnOpen.Text = 'Open';                $btnOpen.Width = 90
$btnBrowse = New-Object System.Windows.Forms.Button; $btnBrowse.Text = 'Open Other Folder...'; $btnBrowse.Width = 170
$btnRemove = New-Object System.Windows.Forms.Button; $btnRemove.Text = 'Remove';              $btnRemove.Width = 90
$buttons.Controls.AddRange(@($btnOpen, $btnBrowse, $btnRemove))

$form.Controls.Add($listBox); $form.Controls.Add($buttons); $form.Controls.Add($hint)

function Refresh-List { $listBox.Items.Clear(); Get-Folders | ForEach-Object { [void]$listBox.Items.Add($_) }; if ($listBox.Items.Count) { $listBox.SelectedIndex = 0 } }
function Open-Selected { if ($listBox.SelectedItem) { Open-Folder ([string]$listBox.SelectedItem); $form.Close() } }

$btnOpen.add_Click({ Open-Selected })
$listBox.add_DoubleClick({ Open-Selected })
$btnBrowse.add_Click({
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = 'Choose a folder to open in Notepad++'
    $dlg.ShowNewFolderButton = $true
    if ($listBox.SelectedItem) { $dlg.SelectedPath = [string]$listBox.SelectedItem }
    if ($dlg.ShowDialog($form) -eq 'OK') { Open-Folder $dlg.SelectedPath; $form.Close() }
})
$btnRemove.add_Click({
    if (-not $listBox.SelectedItem) { return }
    $gone = [string]$listBox.SelectedItem
    Save-Mru (Get-Folders | Where-Object { $_ -ne $gone })
    # Hide it even if it comes from Notepad++'s own history
    Add-Content -LiteralPath $hiddenFile -Value $gone -Encoding UTF8
    Refresh-List
})
$form.add_KeyDown({
    param($s, $e)
    if ($e.KeyCode -eq 'Escape') { $form.Close() }
    elseif ($e.KeyCode -eq 'Enter') { Open-Selected }
    elseif ($e.KeyCode -eq 'Delete') { $btnRemove.PerformClick() }
})

Refresh-List
if ($listBox.Items.Count -eq 0) { $btnBrowse.Select() } else { $listBox.Select() }
[void]$form.ShowDialog()
