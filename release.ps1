# Publish NppDev on GitHub Releases. Builds NppDev-Setup.exe on this PC (build.ps1) and uploads it
# as the release's only file; GitHub publishes its SHA-256, which install.ps1 checks.
#
#   .\release.ps1 -Version 1.6 -Notes "What changed"
#       Writes VERSION, commits, tags v1.6, pushes, builds and creates the release (becomes "latest").
#   .\release.ps1 -Refresh
#       Builds the current version again and replaces the setup file of its release.
#
# Needs the GitHub CLI (gh), logged in.
param([string]$Version, [string]$Notes = '', [switch]$Refresh)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not $Version -and -not $Refresh) { throw 'Give -Version x.y for a new release, or -Refresh to replace the current one' }
$repo = (gh repo view --json nameWithOwner --jq .nameWithOwner)
if (-not $repo) { throw 'Not a GitHub repository (or gh is not logged in)' }
New-Item -ItemType Directory -Force build | Out-Null

function Build {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE) { throw 'build failed' }
}

if ($Version) {
    if ($Version -notmatch '^\d+\.\d+(\.\d+)?$') { throw "Version must look like 1.1 or 1.1.2, not '$Version'" }
    if (git tag --list "v$Version") { throw "v$Version already exists: use -Refresh to replace its setup file" }
    Set-Content VERSION $Version -NoNewline
}
$Version = (Get-Content VERSION -Raw).Trim()
$tag = "v$Version"

if ($Refresh) {
    Build
    gh release upload $tag build\NppDev-Setup.exe --clobber
    if ($LASTEXITCODE) { throw 'upload failed' }
    Write-Host "Replaced NppDev-Setup.exe in release $tag"
} else {
    if (-not $Notes) { $Notes = "NppDev $Version" }
    Build
    $commitFile = Join-Path $PSScriptRoot 'build\commit-message.txt'
    [IO.File]::WriteAllText($commitFile, "Release $Version`n`n$Notes")
    git add -A
    git commit -q -F $commitFile
    if ($LASTEXITCODE) { throw 'commit failed' }
    git tag $tag
    git push origin HEAD
    git push origin $tag
    if ($LASTEXITCODE) { throw 'push failed' }
    $notesFile = Join-Path $PSScriptRoot 'build\release-notes.md'
    [IO.File]::WriteAllText($notesFile, $Notes)
    gh release create $tag build\NppDev-Setup.exe --title "NppDev $Version" --notes-file $notesFile --latest
    if ($LASTEXITCODE) { throw 'release failed' }
    Write-Host "Released $tag"
}
Write-Host "Install: irm https://raw.githubusercontent.com/$repo/main/install.ps1 | iex"
