# Build NppDev-Setup.exe (build\NppDev-Setup.exe): a portable Notepad++ with the Dev Tools plugin,
# settings, theme, font and helpers, packed into one per-user setup program.
#
#   powershell -ExecutionPolicy Bypass -File build.ps1 [-NppDir <a Notepad++ folder>]
#
# Needs MSYS2 gcc (ucrt64 or mingw64) and Windows' tar.exe. The official portable Notepad++ (bundled
# unchanged), NppLspClient and the Java language server are taken from downloads\, and fetched from
# their official releases when missing.
param(
    [string]$NppDir = '',
    [string]$GccBin = ''
)
$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$build = Join-Path $root 'build'
$stage = Join-Path $build 'NppDev'
$dl    = Join-Path $root 'downloads'
$jdtlsName = 'jdt-language-server-1.56.0-202601291528.tar.gz'
$jdtlsUrl  = "https://download.eclipse.org/jdtls/milestones/1.56.0/$jdtlsName"
$lspZip    = Join-Path $dl 'NppLspClient_x64_0.0.40.zip'
$lspUrl    = 'https://github.com/Ekopalypse/NppLspClient/releases/download/v.0.0.40-alpha/NppLspClient_x64_0.0.40.zip'
$nppVersion = '8.9.8.1'
$nppZip    = Join-Path $dl "npp.$nppVersion.portable.x64.zip"
$nppUrl    = "https://github.com/notepad-plus-plus/notepad-plus-plus/releases/download/v$nppVersion/npp.$nppVersion.portable.x64.zip"
function Step($t) { Write-Host "== $t" }

if (-not $GccBin) { $GccBin = Split-Path (Get-ChildItem C:\msys64\ucrt64\bin\gcc.exe, C:\msys64\mingw64\bin\gcc.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName }
if (-not (Test-Path (Join-Path $GccBin 'gcc.exe'))) { throw 'gcc not found: install MSYS2 (ucrt64) or pass -GccBin' }
New-Item -ItemType Directory -Force $dl | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
if (-not $NppDir) {                                    # the official portable build, unpacked in downloads\npp-portable
    $NppDir = Join-Path $dl 'npp-portable'
    if (-not (Test-Path (Join-Path $NppDir 'notepad++.exe'))) {
        if (-not (Test-Path $nppZip)) { Step "Downloading Notepad++ $nppVersion (portable)"; Invoke-WebRequest $nppUrl -OutFile $nppZip -UseBasicParsing }
        Expand-Archive $nppZip $NppDir -Force
    }
}
if (-not (Test-Path (Join-Path $NppDir 'notepad++.exe'))) { throw "Notepad++ not found in $NppDir" }
if (-not (Test-Path $lspZip)) { Step 'Downloading NppLspClient 0.0.40'; Invoke-WebRequest $lspUrl -OutFile $lspZip -UseBasicParsing }
$env:PATH = "$GccBin;$env:PATH"
$gcc = Join-Path $GccBin 'gcc.exe'

if (Test-Path $build) { Remove-Item -LiteralPath $build -Recurse -Force }
New-Item -ItemType Directory -Force $stage, $dl | Out-Null

$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
$parts = $version.Split('.') + '0'
Step "NppDev $version"
Set-Content (Join-Path $build 'version.h') "#define VER_MAJOR $([int]$parts[0])`n#define VER_MINOR $([int]$parts[1])`n#define VER_TEXT `"$version`"`n" -Encoding ASCII

Step 'Compiling the plugin and pyrun'
$plugin = Join-Path $root 'plugin'
# version information (author, product) for the DLL and pyrun.exe: plugin\fileinfo.rc with these names
$windres = Join-Path $GccBin 'windres.exe'
foreach ($f in @(@('plugin', 'NppDev Dev Tools plugin for Notepad++', 'RecentFolders.dll', 'VFT_DLL'),
                 @('pyrun', 'NppDev helper: starts Python for Run and IntelliSense', 'pyrun.exe', 'VFT_APP'))) {
    Set-Content (Join-Path $build "$($f[0])-info.rc") "#define FILE_DESC `"$($f[1])`"`n#define FILE_NAME `"$($f[2])`"`n#define FILE_TYPE $($f[3])`n#include `"fileinfo.rc`"`n" -Encoding ASCII
    & $windres -I $build -I $plugin (Join-Path $build "$($f[0])-info.rc") -O coff -o (Join-Path $build "$($f[0])-info.res")
    if ($LASTEXITCODE) { throw "version info ($($f[0])) failed" }
}
& $gcc -O2 -Wall -shared -municode -o (Join-Path $build 'RecentFolders.dll') (Join-Path $plugin 'RecentFolders.c') (Join-Path $plugin 'runpanel.c') (Join-Path $plugin 'picker.c') (Join-Path $plugin 'explorer.c') (Join-Path $plugin 'welcome.c') (Join-Path $plugin 'update.c') -I $build `
    (Join-Path $build 'plugin-info.res') -luser32 -lgdi32 -lcomctl32 -lshell32 -lole32 -luuid -lwinhttp -lbcrypt -static-libgcc
if ($LASTEXITCODE) { throw 'plugin build failed' }
& $gcc -O2 -Wall -municode -mwindows -o (Join-Path $build 'pyrun.exe') (Join-Path $plugin 'pyrun.c') (Join-Path $build 'pyrun-info.res') -lversion -lshlwapi -static-libgcc
if ($LASTEXITCODE) { throw 'pyrun build failed' }
Remove-Item (Join-Path $build 'nppdev.ico') -ErrorAction SilentlyContinue   # the setup's icon: installer\nppdev.ico (make-icon.py)

Step 'Notepad++ program files (without its updater, uninstaller and plugins)'
foreach ($item in Get-ChildItem $NppDir) {
    if ($item.Name -in 'plugins', 'updater', 'uninstall.exe', 'source code', 'new 1.txt') { continue }   # license and readme stay
    Copy-Item -LiteralPath $item.FullName -Destination $stage -Recurse
}
New-Item -ItemType File -Force (Join-Path $stage 'doLocalConf.xml') | Out-Null     # portable: settings live next to the exe

Step 'Plugins'
$plugins = Join-Path $stage 'plugins'
New-Item -ItemType Directory $plugins, (Join-Path $plugins 'RecentFolders') | Out-Null
foreach ($p in 'mimeTools', 'NppConverter', 'NppExport', 'Config') {
    if (Test-Path (Join-Path $NppDir "plugins\$p")) { Copy-Item (Join-Path $NppDir "plugins\$p") $plugins -Recurse }
}
Get-ChildItem (Join-Path $plugins 'Config') -Directory -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
Get-ChildItem (Join-Path $plugins 'Config') -File -ErrorAction SilentlyContinue | Where-Object Extension -ne '.dll' | Remove-Item -Force
Copy-Item (Join-Path $build 'RecentFolders.dll') (Join-Path $plugins 'RecentFolders')
if (-not (Test-Path $lspZip)) { throw "NppLspClient 0.0.40 x64 zip missing: put it at $lspZip (https://github.com/Ekopalypse/NppLspClient/releases)" }
tar -xf $lspZip -C $plugins                                        # the zip holds an NppLspClient folder
if ($LASTEXITCODE -or -not (Test-Path (Join-Path $plugins 'NppLspClient\NppLspClient.dll'))) { throw 'NppLspClient extract failed' }

Step 'Settings, theme, keys'
$settings = Join-Path $root 'settings'
foreach ($f in 'config.xml', 'shortcuts.xml', 'stylers.xml', 'langs.xml', 'contextMenu.xml') { Copy-Item (Join-Path $settings $f) $stage }
Copy-Item (Join-Path $settings 'themes\*') (Join-Path $stage 'themes') -Force
New-Item -ItemType Directory -Force (Join-Path $stage 'userDefineLangs'), (Join-Path $plugins 'Config\NppLspClient') | Out-Null
Copy-Item (Join-Path $settings 'userDefineLangs\*') (Join-Path $stage 'userDefineLangs')
Copy-Item (Join-Path $settings 'NppLspClient\NppLspClient.toml.template') (Join-Path $plugins 'Config\NppLspClient')

Step 'Helpers and the Java language server'
$dev = Join-Path $stage 'devtools'
New-Item -ItemType Directory $dev, (Join-Path $dev 'jdtls') | Out-Null
Copy-Item (Join-Path $root 'devtools\*') $dev
Copy-Item (Join-Path $build 'pyrun.exe') $dev
$jdtls = Join-Path $dl $jdtlsName
if (-not (Test-Path $jdtls)) { Step "Downloading $jdtlsName"; Invoke-WebRequest $jdtlsUrl -OutFile $jdtls -UseBasicParsing }
tar -xzf $jdtls -C (Join-Path $dev 'jdtls')
if ($LASTEXITCODE) { throw 'jdtls extract failed' }
Get-ChildItem (Join-Path $dev 'jdtls') -Directory | Where-Object { $_.Name -like 'config_*' -and $_.Name -ne 'config_win' } | Remove-Item -Recurse -Force

Step 'Font, notices and sources'
New-Item -ItemType Directory (Join-Path $stage 'fonts'), (Join-Path $stage 'source') | Out-Null
Copy-Item (Join-Path $root 'fonts\*.ttf') (Join-Path $stage 'fonts')
Copy-Item (Join-Path $root 'installer\README-bundle.txt') (Join-Path $stage 'README.txt')
Copy-Item (Join-Path $root 'plugin\*'), (Join-Path $root 'installer\*') (Join-Path $stage 'source')

Step 'Privacy check'
$me = [regex]::Escape($env:USERPROFILE)
$leaks = Get-ChildItem $stage -Recurse -File -Include *.xml, *.toml, *.template, *.ini, *.txt, *.py, *.ps1, *.c, *.h |
         Select-String -Pattern $me -List
if ($leaks) { $leaks | ForEach-Object { Write-Host "  personal path in $($_.Path): $($_.Line.Trim())" }; throw 'personal paths found' }
Write-Host '  no personal paths'

Step 'Packing NppDev-Setup.exe'
$zip = Join-Path $build 'payload.zip'
Push-Location $stage; tar -a -cf $zip *; $code = $LASTEXITCODE; Pop-Location
if ($code) { throw 'zip failed' }
& (Join-Path $GccBin 'windres.exe') -I $build -I (Join-Path $root 'installer') (Join-Path $root 'installer\setup.rc') -O coff -o (Join-Path $build 'setup.res')
if ($LASTEXITCODE) { throw 'resources failed' }
$stub = Join-Path $build 'setup-stub.exe'
& $gcc -O2 -Wall -municode -mwindows -I $build -o $stub (Join-Path $root 'installer\setup.c') (Join-Path $build 'setup.res') `
    -lcomctl32 -lshell32 -lole32 -luuid -ladvapi32 -lgdi32 -static -static-libgcc
if ($LASTEXITCODE) { throw 'setup build failed' }
# NppDev-Setup.exe = setup program + payload.zip + 8-byte payload size + "NPPDEVPK"
$out = Join-Path $build 'NppDev-Setup.exe'
$fs = [IO.File]::Create($out)
foreach ($part in $stub, $zip) { $b = [IO.File]::ReadAllBytes($part); $fs.Write($b, 0, $b.Length) }
$fs.Write([BitConverter]::GetBytes([int64](Get-Item $zip).Length), 0, 8)
$tag = [Text.Encoding]::ASCII.GetBytes('NPPDEVPK'); $fs.Write($tag, 0, 8)
$fs.Close()
Step ("Done: {0}  ({1:N1} MB)" -f $out, ((Get-Item $out).Length / 1MB))
