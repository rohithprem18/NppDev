# NppDev one-line installer. Paste into PowerShell (Windows key, type "powershell", Enter):
#
#     irm https://raw.githubusercontent.com/rohithprem18/NppDev/main/install.ps1 | iex
#
# It always takes the newest release of this repository: it asks GitHub which release is the latest,
# downloads its NppDev-Setup.exe, checks it against the SHA-256 that GitHub publishes for that file,
# and starts the setup. Files downloaded this way are not marked as "from the internet", so Windows
# SmartScreen doesn't stop the setup. Nothing else is downloaded or changed; the setup installs for
# your user only (no administrator rights), and updates an NppDev that is already installed.
#
# Optional, set before running:  $env:NPPDEV_SETUP_ARGS = '/S'   (silent install)
& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'          # Windows PowerShell downloads far faster without it
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $repo = 'rohithprem18/NppDev'
    $work = Join-Path ([IO.Path]::GetTempPath()) ('nppdev-install-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $exe = Join-Path $work 'NppDev-Setup.exe'

    Write-Host ''
    Write-Host '  NppDev - Notepad++ Dev Edition' -ForegroundColor Cyan
    Write-Host "  https://github.com/$repo" -ForegroundColor DarkGray
    Write-Host ''
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        Write-Host '  Finding the latest release...'
        $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" -Headers @{ 'User-Agent' = 'NppDev-installer'; 'Accept' = 'application/vnd.github+json' }
        $asset = $release.assets | Where-Object { $_.name -eq 'NppDev-Setup.exe' } | Select-Object -First 1
        if (-not $asset) { throw "The latest release ($($release.tag_name)) has no NppDev-Setup.exe." }
        $want = "$($asset.digest)" -replace '^sha256:', ''

        Write-Host "  Downloading NppDev $($release.tag_name.TrimStart('v'))..."
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $exe -UseBasicParsing -Headers @{ 'User-Agent' = 'NppDev-installer' }
        $got = (Get-FileHash -Algorithm SHA256 -Path $exe).Hash
        if (-not $want) { throw "GitHub gave no checksum for this file, so it can't be verified. Nothing was installed." }
        if ($want.ToUpperInvariant() -ne $got.ToUpperInvariant()) {
            throw "The download doesn't match GitHub's checksum (expected $want, got $got). Nothing was installed; please try again."
        }
        Write-Host ('  Verified: {0:N1} MB, SHA-256 {1}...' -f ((Get-Item $exe).Length / 1MB), $got.Substring(0, 16).ToLowerInvariant()) -ForegroundColor Green

        $setupArgs = if ($env:NPPDEV_SETUP_ARGS) { $env:NPPDEV_SETUP_ARGS } else { '' }
        Write-Host '  Starting the setup...'
        $p = if ($setupArgs) { Start-Process -FilePath $exe -ArgumentList $setupArgs -PassThru } else { Start-Process -FilePath $exe -PassThru }
        $p.WaitForExit()                              # the setup only: not NppDev, which it may start
        if ($p.ExitCode -eq 0) { Write-Host '  Done.' -ForegroundColor Green }
        else { Write-Host "  The setup ended with code $($p.ExitCode)." -ForegroundColor Yellow }
    } catch {
        Write-Host "  $($_.Exception.Message)" -ForegroundColor Red
    } finally {
        Start-Sleep -Milliseconds 500
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host ''
    }
}
