# NppDev installer. Paste into PowerShell (Windows key, type "powershell", Enter):
#
#     irm https://raw.githubusercontent.com/rohithprem18/NppDev/main/install.ps1 | iex
#
# What it does, for your user only (no administrator rights, no setup program):
#   1. asks GitHub for the latest release and downloads its NppDev.exe,
#   2. checks the file against the SHA-256 that GitHub publishes for it,
#   3. puts it in %LOCALAPPDATA%\Programs\NppDev, adds NppDev to the Start menu,
#      adds "Open with NppDev" to File Explorer and lists NppDev under "Open with" for code files,
#   4. registers NppDev in Settings > Apps, so it can be uninstalled like any other app.
# Running it again updates NppDev in place; your settings and test cases are kept.
#
# Optional, set before running:
#   $env:NPPDEV_EXE = 'C:\path\to\nppdev.exe'   install this file instead of downloading
#   $env:NPPDEV_NO_LAUNCH = '1'                 don't start NppDev at the end
& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'          # Windows PowerShell downloads far faster without it
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $repo = 'rohithprem18/NppDev'
    $dir = Join-Path $env:LOCALAPPDATA 'Programs\NppDev'
    $target = Join-Path $dir 'nppdev.exe'
    $work = Join-Path ([IO.Path]::GetTempPath()) ('nppdev-install-' + [guid]::NewGuid().ToString('N').Substring(0, 8))

    function Set-Key($path, $values) {
        if (-not (Test-Path -LiteralPath $path)) { New-Item -Path $path -Force | Out-Null }
        foreach ($k in $values.Keys) { New-ItemProperty -LiteralPath $path -Name $k -Value $values[$k] -PropertyType String -Force | Out-Null }
    }

    Write-Host ''
    Write-Host '  NppDev' -ForegroundColor White
    Write-Host "  https://github.com/$repo" -ForegroundColor DarkGray
    Write-Host ''
    try {
        # NppDev draws its window with Microsoft Edge WebView2, which Windows 10 and 11 normally have
        $wv = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
        $hasWebView = Get-ItemProperty "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$wv", "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$wv", "HKCU:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$wv" -ErrorAction SilentlyContinue |
            Where-Object { $_.pv -and $_.pv -ne '0.0.0.0' }
        if (-not $hasWebView) {
            Write-Host '  Microsoft Edge WebView2 is missing. Install it from https://go.microsoft.com/fwlink/p/?LinkId=2124703, then run this again.' -ForegroundColor Yellow
            return
        }

        New-Item -ItemType Directory -Path $work | Out-Null
        $src = Join-Path $work 'nppdev.exe'
        if ($env:NPPDEV_EXE) {
            Copy-Item -LiteralPath $env:NPPDEV_EXE -Destination $src
            Write-Host "  Using $($env:NPPDEV_EXE)"
        } else {
            Write-Host '  Finding the latest release...'
            $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" -Headers @{ 'User-Agent' = 'NppDev-installer'; 'Accept' = 'application/vnd.github+json' }
            $asset = $release.assets | Where-Object { $_.name -eq 'NppDev.exe' } | Select-Object -First 1
            if (-not $asset) { throw "The latest release ($($release.tag_name)) has no NppDev.exe." }
            $want = "$($asset.digest)" -replace '^sha256:', ''
            Write-Host "  Downloading NppDev $($release.tag_name.TrimStart('v'))..."
            Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $src -UseBasicParsing -Headers @{ 'User-Agent' = 'NppDev-installer' }
            $got = (Get-FileHash -Algorithm SHA256 -Path $src).Hash
            if (-not $want) { throw "GitHub gave no checksum for this file, so it can't be verified. Nothing was installed." }
            if ($want.ToUpperInvariant() -ne $got.ToUpperInvariant()) {
                throw "The download doesn't match GitHub's checksum (expected $want, got $got). Nothing was installed; please try again."
            }
            Write-Host ('  Verified: {0:N1} MB, SHA-256 {1}...' -f ((Get-Item $src).Length / 1MB), $got.Substring(0, 16).ToLowerInvariant()) -ForegroundColor Green
        }

        # an older NppDev (the Notepad++-based 1.x) in the same folder is removed first; settings are kept
        $old = Join-Path $dir 'uninstall.exe'
        if (Test-Path -LiteralPath (Join-Path $dir 'notepad++.exe')) {
            if (Test-Path -LiteralPath $old) {
                Write-Host '  Removing the older NppDev 1.x first...'
                Start-Process -FilePath $old -ArgumentList '/uninstall', '/S' -Wait
            }
        }

        # an open NppDev holds its file: wait for it to be closed (unsaved work stays safe)
        $isOpen = { Get-Process nppdev -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $target } }
        if (& $isOpen) {
            Write-Host '  NppDev is open. Close it to continue (waiting up to 2 minutes)...' -ForegroundColor Yellow
            $deadline = (Get-Date).AddMinutes(2)
            while ((& $isOpen) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
            if (& $isOpen) { throw 'NppDev is still open. Nothing was changed; close it and run this again.' }
        }

        Write-Host "  Installing to $dir"
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        Copy-Item -LiteralPath $src -Destination $target -Force
        $version = "$((Get-Item $target).VersionInfo.ProductVersion)"

        # Start menu
        $lnk = Join-Path ([Environment]::GetFolderPath('Programs')) 'NppDev.lnk'
        $s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
        $s.TargetPath = $target
        $s.WorkingDirectory = $dir
        $s.IconLocation = "$target,0"
        $s.Description = 'Write, run and judge your code.'
        $s.Save()

        # "Open with NppDev" on files, folders and the inside of folders (Windows 11: under "Show more options")
        $cls = 'HKCU:\Software\Classes'
        foreach ($place in @(@('*', '%1'), @('Directory', '%V'), @('Directory\Background', '%V'))) {
            Set-Key "$cls\$($place[0])\shell\NppDev" @{ '(default)' = 'Open with NppDev'; 'Icon' = $target }
            Set-Key "$cls\$($place[0])\shell\NppDev\command" @{ '(default)' = "`"$target`" `"$($place[1])`"" }
        }
        # NppDev in the "Open with" list of code and text files (your default apps are not changed)
        Set-Key "$cls\NppDev.File" @{ '(default)' = 'Code file' }
        Set-Key "$cls\NppDev.File\DefaultIcon" @{ '(default)' = "$target,0" }
        Set-Key "$cls\NppDev.File\shell\open\command" @{ '(default)' = "`"$target`" `"%1`"" }
        Set-Key "$cls\Applications\nppdev.exe" @{ 'FriendlyAppName' = 'NppDev' }
        Set-Key "$cls\Applications\nppdev.exe\shell\open\command" @{ '(default)' = "`"$target`" `"%1`"" }
        foreach ($e in ('.c .cpp .cc .cxx .h .hpp .java .py .js .mjs .ts .go .rs .kt .cs .txt .md .json .in .out .ini .toml .sh .ps1' -split ' ')) {
            Set-Key "$cls\$e\OpenWithProgids" @{ 'NppDev.File' = '' }
            Set-Key "$cls\Applications\nppdev.exe\SupportedTypes" @{ $e = '' }
        }

        # uninstaller, and the entry in Settings > Apps
        $uninstall = Join-Path $dir 'uninstall.ps1'
        @'
# Removes NppDev (installed by install.ps1). Your settings and test cases in %APPDATA% are kept.
$ErrorActionPreference = 'SilentlyContinue'
Get-Process nppdev | Where-Object { $_.Path -like "$PSScriptRoot*" } | Stop-Process -Force
$cls = 'HKCU:\Software\Classes'
foreach ($k in '*\shell\NppDev', 'Directory\shell\NppDev', 'Directory\Background\shell\NppDev', 'NppDev.File', 'Applications\nppdev.exe') { Remove-Item -LiteralPath "$cls\$k" -Recurse -Force }
foreach ($e in ('.c .cpp .cc .cxx .h .hpp .java .py .js .mjs .ts .go .rs .kt .cs .txt .md .json .in .out .ini .toml .sh .ps1' -split ' ')) { Remove-ItemProperty -LiteralPath "$cls\$e\OpenWithProgids" -Name 'NppDev.File' }
Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Programs')) 'NppDev.lnk') -Force
Remove-Item -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\NppDev' -Recurse -Force
Start-Process cmd.exe -ArgumentList "/c timeout /t 2 >nul & rmdir /s /q `"$PSScriptRoot`"" -WindowStyle Hidden
'@ | Set-Content -LiteralPath $uninstall -Encoding UTF8
        $u = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\NppDev'
        Set-Key $u @{
            'DisplayName' = 'NppDev'; 'DisplayVersion' = $version; 'Publisher' = 'Rohith Prem'; 'DisplayIcon' = "$target,0"
            'InstallLocation' = $dir; 'URLInfoAbout' = "https://github.com/$repo"
            'UninstallString' = "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$uninstall`""
        }
        foreach ($n in 'NoModify', 'NoRepair') { New-ItemProperty -LiteralPath $u -Name $n -Value 1 -PropertyType DWord -Force | Out-Null }
        New-ItemProperty -LiteralPath $u -Name 'EstimatedSize' -Value ([int]((Get-Item $target).Length / 1KB)) -PropertyType DWord -Force | Out-Null

        # tell File Explorer that the "Open with" entries changed
        if (-not ('NppDevInstall.Shell' -as [type])) {
            Add-Type -Namespace NppDevInstall -Name Shell -MemberDefinition '[DllImport("shell32.dll")] public static extern void SHChangeNotify(int e, int f, System.IntPtr a, System.IntPtr b);'
        }
        [NppDevInstall.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)

        Write-Host "  Installed NppDev $version." -ForegroundColor Green
        Write-Host '  Start menu: NppDev. Right-click a file or folder: Open with NppDev.' -ForegroundColor DarkGray
        Write-Host '  To keep it on the taskbar: right-click its taskbar icon, then Pin to taskbar.' -ForegroundColor DarkGray
        if (-not $env:NPPDEV_NO_LAUNCH) { Start-Process -FilePath $target }
    } catch {
        Write-Host "  $($_.Exception.Message)" -ForegroundColor Red
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host ''
    }
}
