# NppDev - Check / Install Tools
# Shows which compilers and language servers this PC has, lets the user pick a version when several
# are installed, and installs what is missing or too old (winget / pip) after the user agrees.
#   -StartupCheck     at every NppDev start: opens only when something is missing or too old
#                     (unless "Check at every start" was turned off). -FirstRun is the old name.
#   -Install gpp,java install these right away (Run asked for them); exit code 0 when they are
#                     installed afterwards, so the Run panel can run the program
param([switch]$StartupCheck, [switch]$FirstRun, [string]$Install = '')

$ErrorActionPreference = 'Stop'
$StartupCheck = $StartupCheck -or $FirstRun
$wanted = @($Install -split '[,\s]+' | Where-Object { $_ })
$here  = $PSScriptRoot
$pyrun = Join-Path $here 'pyrun.exe'
$ini   = Join-Path $here 'tools.ini'
$script:exitCode = 1
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

# what to install for each tool (winget package id, or pip)
$winlibs = @{ label = 'GCC / G++ 14 + clangd (WinLibs)'; winget = 'BrechtSanders.WinLibs.POSIX.UCRT.LLVM'; extra = @() }
$temurin = @{ label = 'Java JDK 21 (Temurin)'; winget = 'EclipseAdoptium.Temurin.21.JDK'; extra = @() }
$packages = [ordered]@{
    python   = @{ label = 'Python 3.13'; winget = 'Python.Python.3.13'; extra = @('--scope', 'user') }
    gpp      = $winlibs
    clangd   = $winlibs
    java     = $temurin
    java_lsp = $temurin
    pylsp    = @{ label = 'pylsp (Python IntelliSense)'; pip = 'python-lsp-server' }
    node     = @{ label = 'Node.js LTS (runs JavaScript)'; winget = 'OpenJS.NodeJS.LTS'; extra = @() }
    js_lsp   = @{ label = 'JavaScript IntelliSense (TypeScript 7)'; npm = @('typescript@latest') }
}
$links = @{
    python = 'https://www.python.org/downloads/'; gpp = 'https://winlibs.com/'; clangd = 'https://winlibs.com/'
    java = 'https://adoptium.net/'; java_lsp = 'https://adoptium.net/'; pylsp = 'pip install python-lsp-server'
    node = 'https://nodejs.org/'; js_lsp = 'npm install -g typescript'
}

# ---------- tools.ini: [paths] picks, [state] startup_check ----------
function Read-Ini {
    $data = @{}; $section = ''
    if (Test-Path $ini) {
        foreach ($line in Get-Content $ini) {
            if ($line -match '^\s*\[(.+)\]\s*$') { $section = $Matches[1].ToLower(); if (-not $data[$section]) { $data[$section] = [ordered]@{} } }
            elseif ($line -match '^\s*([^=;#]+?)\s*=\s*(.*)$' -and $section) { $data[$section][$Matches[1].ToLower()] = $Matches[2] }
        }
    }
    $data
}
function Set-Ini([string]$section, [string]$key, [string]$value) {
    $data = Read-Ini
    if (-not $data[$section]) { $data[$section] = [ordered]@{} }
    if ($value) { $data[$section][$key] = $value } else { $data[$section].Remove($key) }
    $out = foreach ($s in $data.Keys) { "[$s]"; foreach ($k in $data[$s].Keys) { "$k = $($data[$s][$k])" }; '' }
    Set-Content -LiteralPath $ini -Value $out -Encoding UTF8
}

# ---------- running helpers ----------
# Run a program, keep the window responsive, and hand each piece of its output to $onText as it
# arrives (for the progress bar: winget redraws its download progress on one line with CR).
function Invoke-Captured([string]$exe, [string[]]$arguments, [scriptblock]$onText = $null) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo $exe
    $psi.Arguments = ($arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $streams = @($p.StandardOutput.BaseStream, $p.StandardError.BaseStream)
    $bufs = @((New-Object byte[] 8192), (New-Object byte[] 8192))
    $decoders = @([System.Text.Encoding]::UTF8.GetDecoder(), [System.Text.Encoding]::UTF8.GetDecoder())
    $texts = @((New-Object System.Text.StringBuilder), (New-Object System.Text.StringBuilder))
    $tasks = @($streams[0].ReadAsync($bufs[0], 0, 8192), $streams[1].ReadAsync($bufs[1], 0, 8192))
    $open = @($true, $true)
    while ($open[0] -or $open[1]) {
        $got = $false
        for ($i = 0; $i -lt 2; $i++) {
            if (-not $open[$i] -or -not $tasks[$i].IsCompleted) { continue }
            $n = $tasks[$i].Result
            if ($n -le 0) { $open[$i] = $false; continue }
            $chars = New-Object char[] ($n + 4)
            $c = $decoders[$i].GetChars($bufs[$i], 0, $n, $chars, 0)
            $chunk = New-Object string ($chars, 0, $c)
            [void]$texts[$i].Append($chunk)
            if ($onText) { & $onText $chunk }
            $tasks[$i] = $streams[$i].ReadAsync($bufs[$i], 0, 8192)
            $got = $true
        }
        [System.Windows.Forms.Application]::DoEvents()
        if (-not $got) { Start-Sleep -Milliseconds 40 }
    }
    $p.WaitForExit()
    @{ code = $p.ExitCode; out = $texts[0].ToString(); err = $texts[1].ToString() }
}

function Get-Report {
    $py = ''
    try { $py = (Invoke-Captured $pyrun @('--where')).out.Trim() } catch { }
    if ($py) {
        $r = Invoke-Captured $py @('-I', (Join-Path $here 'tools.py'), '--report')
        # Windows PowerShell hands a JSON array over as one object: unroll it
        if ($r.code -eq 0 -and $r.out) { return @{ python = $py; tools = @(($r.out | ConvertFrom-Json) | ForEach-Object { $_ }) } }
    }
    $unknown = 'checked after Python is installed'
    @{ python = $null; tools = @(
        [pscustomobject]@{ key = 'python'; name = 'Python 3.9+'; need = 'Runs .py files and the editor helpers'; ok = $false; path = ''; version = ''; candidates = @() }
        [pscustomobject]@{ key = 'gpp'; name = 'C / C++ compiler (gcc, g++)'; need = $unknown; ok = $null; path = ''; version = ''; candidates = @() }
        [pscustomobject]@{ key = 'clangd'; name = 'C / C++ IntelliSense (clangd)'; need = $unknown; ok = $null; path = ''; version = ''; candidates = @() }
        [pscustomobject]@{ key = 'java'; name = 'Java (JDK)'; need = $unknown; ok = $null; path = ''; version = ''; candidates = @() }
        [pscustomobject]@{ key = 'java_lsp'; name = 'Java IntelliSense (JDK 21+)'; need = $unknown; ok = $null; path = ''; version = ''; candidates = @() }
        [pscustomobject]@{ key = 'pylsp'; name = 'Python IntelliSense (pylsp)'; need = $unknown; ok = $null; path = ''; version = ''; candidates = @() }
        [pscustomobject]@{ key = 'node'; name = 'JavaScript (Node.js)'; need = $unknown; ok = $null; path = ''; version = ''; candidates = @() }
        [pscustomobject]@{ key = 'js_lsp'; name = 'JavaScript IntelliSense'; need = $unknown; ok = $null; path = ''; version = ''; candidates = @() }) }
}

function Get-Missing($report) { @($report.tools | Where-Object { $_.ok -ne $true } | ForEach-Object { $_.key }) }

# ---------- at start-up: say nothing when everything is there ----------
$report = Get-Report
if ($StartupCheck) {
    if ((Read-Ini)['state'] -and (Read-Ini)['state']['startup_check'] -eq '0') { exit 0 }
    # JavaScript is optional: missing Node.js / TypeScript alone doesn't open this window at start
    # (running a .js file offers to install Node.js, and the window lists both)
    if (@(Get-Missing $report | Where-Object { $_ -notin 'node', 'js_lsp' }).Count -eq 0) { exit 0 }
}

# ---------- window ----------
$ok = [char]0x2714; $bad = [char]0x2716; $warn = [char]0x26A0
$form = New-Object System.Windows.Forms.Form
$form.Text = 'NppDev - Check / Install Tools'
$form.Size = New-Object System.Drawing.Size(920, 720)
$form.MinimumSize = New-Object System.Drawing.Size(760, 560)
$form.StartPosition = 'CenterScreen'
$form.Font = New-Object System.Drawing.Font('Segoe UI', 9.5)
$form.TopMost = $wanted.Count -gt 0                  # asked for from Run: keep it in sight

$intro = New-Object System.Windows.Forms.Label
$intro.Dock = 'Top'; $intro.Height = 52; $intro.Padding = New-Object System.Windows.Forms.Padding(12, 10, 12, 0)
$intro.Text = if ($wanted.Count) { "Installing what your program needs. When it's done, NppDev runs your program." }
              elseif ($StartupCheck) { "Some tools NppDev needs for Run (Ctrl+Enter) and IntelliSense are missing or too old. Click Install to add them; tools you already have stay as they are." }
              else { "Tools used by Run (Ctrl+Enter) and IntelliSense. When several versions are installed, pick the one to use; 'automatic' prefers PATH, then the newest." }

$grid = New-Object System.Windows.Forms.TableLayoutPanel
$grid.Dock = 'Top'; $grid.Height = 380; $grid.ColumnCount = 3; $grid.AutoScroll = $true
$grid.Padding = New-Object System.Windows.Forms.Padding(12, 4, 12, 4)
[void]$grid.ColumnStyles.Add((New-Object System.Windows.Forms.ColumnStyle('Absolute', 270)))
[void]$grid.ColumnStyles.Add((New-Object System.Windows.Forms.ColumnStyle('Absolute', 140)))
[void]$grid.ColumnStyles.Add((New-Object System.Windows.Forms.ColumnStyle('Percent', 100)))

$log = New-Object System.Windows.Forms.TextBox
$log.Dock = 'Fill'; $log.Multiline = $true; $log.ReadOnly = $true; $log.ScrollBars = 'Vertical'
$log.Font = New-Object System.Drawing.Font('Consolas', 9)

$buttons = New-Object System.Windows.Forms.FlowLayoutPanel
$buttons.Dock = 'Bottom'; $buttons.Height = 46; $buttons.FlowDirection = 'RightToLeft'; $buttons.Padding = New-Object System.Windows.Forms.Padding(8)
$btnClose = New-Object System.Windows.Forms.Button; $btnClose.Text = 'Close'; $btnClose.Width = 90
$btnCheck = New-Object System.Windows.Forms.Button; $btnCheck.Text = 'Check again'; $btnCheck.Width = 110
$btnInstall = New-Object System.Windows.Forms.Button; $btnInstall.Width = 210
$atStart = New-Object System.Windows.Forms.CheckBox; $atStart.Text = 'Check at every start'; $atStart.AutoSize = $true
$atStart.Margin = New-Object System.Windows.Forms.Padding(12, 8, 12, 0)
$s0 = (Read-Ini)['state']; $atStart.Checked = -not ($s0 -and $s0['startup_check'] -eq '0')
$atStart.add_CheckedChanged({ Set-Ini 'state' 'startup_check' $(if ($atStart.Checked) { '1' } else { '0' }) })
$buttons.Controls.AddRange(@($btnClose, $btnCheck, $btnInstall, $atStart))

# progress while installing: "Step 2 of 4 - Installing ... - 45.2 MB / 90.4 MB" over a green bar
$progressPanel = New-Object System.Windows.Forms.Panel
$progressPanel.Dock = 'Bottom'; $progressPanel.Height = 58; $progressPanel.Padding = New-Object System.Windows.Forms.Padding(12, 6, 12, 6)
$progressPanel.Visible = $false
$progressText = New-Object System.Windows.Forms.Label
$progressText.Dock = 'Top'; $progressText.Height = 22; $progressText.AutoEllipsis = $true
$progressBar = New-Object System.Windows.Forms.ProgressBar
$progressBar.Dock = 'Top'; $progressBar.Height = 20; $progressBar.Minimum = 0; $progressBar.Maximum = 1000
$progressBar.Style = 'Continuous'
$progressPanel.Controls.Add($progressBar); $progressPanel.Controls.Add($progressText)

$form.Controls.Add($log); $form.Controls.Add($progressPanel); $form.Controls.Add($buttons); $form.Controls.Add($grid); $form.Controls.Add($intro)

function Say([string]$text) { $log.AppendText($text + "`r`n"); [System.Windows.Forms.Application]::DoEvents() }

# ---------- progress ----------
$script:stepIndex = 0; $script:stepCount = 1; $script:stepLabel = ''; $script:stepPart = 0.0; $script:tail = ''
function Set-Progress([double]$part, [string]$detail) {
    if ($part -gt $script:stepPart) { $script:stepPart = [Math]::Min($part, 1.0) }
    $overall = ($script:stepIndex + $script:stepPart) / [Math]::Max($script:stepCount, 1)
    $progressBar.Value = [int][Math]::Min(1000, [Math]::Max(0, $overall * 1000))
    if ($script:stepIndex -ge $script:stepCount) { $text = 'Checking what was installed...' }
    else {
        $text = "Step $($script:stepIndex + 1) of $($script:stepCount)  -  $($script:stepLabel)"
        if ($detail) { $text += "  -  $detail" }
    }
    $text += "   ($([int]($overall * 100))%)"
    $progressText.Text = $text
    [System.Windows.Forms.Application]::DoEvents()
}
# Read winget / npm / pip output as it comes: download sizes ("45.2 MB / 90.4 MB") or percentages
# move the bar through the first 75% of the step; installing moves it on slowly to 95%.
$unitBytes = @{ 'B' = 1.0; 'KB' = 1KB; 'MB' = 1MB; 'GB' = 1GB }
$onInstallerText = {
    param($chunk)
    $script:tail = ($script:tail + $chunk)
    if ($script:tail.Length -gt 600) { $script:tail = $script:tail.Substring($script:tail.Length - 600) }
    $ms = [regex]::Matches($script:tail, '([\d.]+)\s*(B|KB|MB|GB)\s*/\s*([\d.]+)\s*(B|KB|MB|GB)')
    if ($ms.Count) {
        $m = $ms[$ms.Count - 1]
        $done = [double]$m.Groups[1].Value * $unitBytes[$m.Groups[2].Value]; $all = [double]$m.Groups[3].Value * $unitBytes[$m.Groups[4].Value]
        if ($all -gt 0) { $script:sawNumbers = $true; Set-Progress (0.75 * $done / $all) "downloading $($m.Value)"; return }
    }
    $ps = [regex]::Matches($script:tail, '(\d{1,3})\s?%')
    if ($ps.Count -and $script:tail -notmatch 'install') { $script:sawNumbers = $true; Set-Progress (0.75 * [int]$ps[$ps.Count - 1].Groups[1].Value / 100) 'downloading'; return }
    if ($script:tail -match 'verified installer hash|Starting package install|Installing|added \d+ packages|Successfully installed') {
        Set-Progress ([Math]::Max($script:stepPart, 0.8)) 'installing'
    }
}
$creep = New-Object System.Windows.Forms.Timer; $creep.Interval = 700     # installing has no numbers: keep moving gently
$creep.add_Tick({
    if (-not $progressPanel.Visible -or $script:stepIndex -ge $script:stepCount) { return }
    if ($script:stepPart -ge 0.75 -and $script:stepPart -lt 0.95) { Set-Progress ($script:stepPart + 0.01) 'installing' }
    elseif (-not $script:sawNumbers -and $script:stepPart -lt 0.75) { Set-Progress ($script:stepPart + 0.02) 'working' }   # npm, pip: no numbers to show
})
function Start-Step([string]$label) { $script:stepLabel = $label; $script:stepPart = 0.0; $script:tail = ''; $script:sawNumbers = $false; Set-Progress 0.02 'starting' }
function End-Step { $script:stepIndex++; $script:stepPart = 0.0; Set-Progress 0 '' }

function Show-Report($report) {
    $grid.SuspendLayout(); $grid.Controls.Clear(); $grid.RowStyles.Clear(); $grid.RowCount = 0
    $picks = (Read-Ini)['paths']
    foreach ($t in $report.tools) {
        $name = New-Object System.Windows.Forms.Label
        $name.Text = "$($t.name)`n$($t.need)"; $name.AutoSize = $false; $name.Height = 40; $name.Dock = 'Fill'
        $state = New-Object System.Windows.Forms.Label
        $state.Dock = 'Fill'; $state.TextAlign = 'MiddleLeft'
        if ($t.ok -eq $true) { $state.Text = "$ok  $($t.version)"; $state.ForeColor = [System.Drawing.Color]::SeaGreen }
        elseif ($t.old) { $state.Text = "$warn  too old ($($t.version))"; $state.ForeColor = [System.Drawing.Color]::DarkOrange }
        elseif ($t.ok -eq $false) { $state.Text = "$bad  missing"; $state.ForeColor = [System.Drawing.Color]::Firebrick }
        else { $state.Text = '?'; $state.ForeColor = [System.Drawing.Color]::Gray }
        $cands = @($t.candidates)
        if ($t.ok -eq $true -and $cands.Count -gt 1 -and $t.key -in 'gpp', 'clangd', 'java', 'java_lsp') {
            $where = New-Object System.Windows.Forms.ComboBox
            $where.DropDownStyle = 'DropDownList'; $where.Dock = 'Fill'
            [void]$where.Items.Add("automatic  ($($t.path))")
            foreach ($c in $cands) { [void]$where.Items.Add("$($c.version)  -  $($c.path)") }
            $picked = if ($picks) { $picks[$t.key] } else { $null }
            $where.SelectedIndex = 0
            for ($i = 0; $i -lt $cands.Count; $i++) { if ($picked -and $cands[$i].path -eq $picked) { $where.SelectedIndex = $i + 1 } }
            $where.Tag = @{ key = $t.key; cands = $cands }
            $where.add_SelectionChangeCommitted({
                param($s, $e)
                $info = $s.Tag; $value = if ($s.SelectedIndex -gt 0) { $info.cands[$s.SelectedIndex - 1].path } else { '' }
                Set-Ini 'paths' $info.key $value
                Say "Saved: $($info.key) = $(if ($value) { $value } else { 'automatic' })"
            })
        } else {
            $where = New-Object System.Windows.Forms.Label
            $where.Dock = 'Fill'; $where.TextAlign = 'MiddleLeft'; $where.AutoEllipsis = $true
            $where.Text = if ($t.ok -eq $true) { $t.path }
                          elseif ($t.old) { "Install $($packages[$t.key].label) next to it (yours stays)" }
                          elseif ($t.ok -eq $false) { "Install: $($packages[$t.key].label)   or get it from $($links[$t.key])" } else { '' }
        }
        $grid.RowCount++
        [void]$grid.RowStyles.Add((New-Object System.Windows.Forms.RowStyle('Absolute', 46)))
        $grid.Controls.Add($name, 0, $grid.RowCount - 1); $grid.Controls.Add($state, 1, $grid.RowCount - 1); $grid.Controls.Add($where, 2, $grid.RowCount - 1)
    }
    $grid.ResumeLayout()
    $missing = Get-Missing $report
    $btnInstall.Text = if ($missing.Count) { "Install missing ($($missing.Count))" } else { 'Everything is installed' }
    $btnInstall.Enabled = $missing.Count -gt 0
}

$blocks = '[' + [char]0x2580 + '-' + [char]0x259F + ']'          # winget's progress-bar characters
function Run-Step([string]$what, [string]$exe, [string[]]$arguments) {
    Say ''; Say "== $what"; Say "> $exe $($arguments -join ' ')"
    Start-Step $what
    $r = Invoke-Captured $exe $arguments $onInstallerText
    End-Step
    foreach ($line in (($r.out + "`n" + $r.err) -split "`r?`n")) {
        $line = ($line -split "`r")[-1].Trim()                     # winget redraws its progress bar with CR
        if ($line -and $line -notmatch '^[\s\-\\|/]+$' -and $line -notmatch $blocks) { Say "  $line" }
    }
    Say $(if ($r.code -eq 0) { "   done" } else { "   finished with code $($r.code)" })
    $r.code -eq 0
}

# Install what is missing or too old; with $only, just those tools (and Python first when it's missing).
function Install-Missing([string[]]$only) {
    $btnInstall.Enabled = $false; $btnCheck.Enabled = $false
    $winget = (Get-Command winget.exe -ErrorAction SilentlyContinue).Source
    $missing = Get-Missing $report
    if ($only) { $missing = @($missing | Where-Object { $_ -in $only -or $_ -eq 'python' }) }
    if (-not $winget -and ($missing | Where-Object { $_ -ne 'pylsp' })) {
        Say 'winget (Windows App Installer) is not available on this PC, so tools cannot be installed automatically.'
        Say 'Get "App Installer" from the Microsoft Store, or install these yourself, then click "Check again":'
        foreach ($k in $missing) { Say "  $($packages[$k].label): $($links[$k])" }
    }
    $done = @{}
    $common = @('--exact', '--silent', '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity')
    $order = 'python', 'gpp', 'clangd', 'java', 'java_lsp', 'node'

    # the steps, counted up front for "Step 2 of 4"
    $plan = @(); $seen = @{}
    foreach ($k in $order) {
        if ($k -in $missing -and $winget -and -not $seen[$packages[$k].winget]) { $seen[$packages[$k].winget] = $true; $plan += $k }
    }
    if ('pylsp' -in $missing) { $plan += 'pylsp' }
    if ('js_lsp' -in $missing -or ('node' -in $plan -and -not $only)) { $plan += 'js_lsp' }
    $script:stepIndex = 0; $script:stepCount = [Math]::Max($plan.Count, 1)
    $progressBar.Value = 0; $progressPanel.Visible = $plan.Count -gt 0; $creep.Start()

    foreach ($k in $order) {
        if ($k -notin $missing -or -not $winget) { continue }
        $pkg = $packages[$k]
        if ($done[$pkg.winget]) { continue }
        $done[$pkg.winget] = $true
        [void](Run-Step "Installing $($pkg.label)" $winget (@('install', '--id', $pkg.winget) + $common + $pkg.extra))
        if ($k -eq 'python') { $script:report = Get-Report }       # now the others can be checked
    }
    $script:report = Get-Report
    if ('pylsp' -in (Get-Missing $report) -and $report.python -and (-not $only -or 'pylsp' -in $only)) {
        [void](Run-Step 'Installing pylsp (Python IntelliSense)' $report.python @('-m', 'pip', 'install', '--user', '--upgrade', 'python-lsp-server'))
        $script:report = Get-Report
    }
    $node = ($report.tools | Where-Object { $_.key -eq 'node' } | Select-Object -First 1)
    if ('js_lsp' -in (Get-Missing $report) -and $node -and $node.path -and ('js_lsp' -in $plan)) {
        # npm's own script, run by node: works for every Node install (winget, nvm, ...) without PATH changes
        $npmCli = Join-Path (Split-Path $node.path) 'node_modules\npm\bin\npm-cli.js'
        if (Test-Path $npmCli) {
            [void](Run-Step "Installing $($packages.js_lsp.label)" $node.path (@($npmCli, 'install', '--global', '--no-fund', '--no-audit') + $packages.js_lsp.npm))
            $script:report = Get-Report
        } else { Say "npm was not found next to $($node.path): install it with $($links.js_lsp)" }
    }
    $creep.Stop()
    Show-Report $report
    $left = @(Get-Missing $report | Where-Object { -not $only -or $_ -in $only -or $_ -eq 'python' })
    Say ''
    if ($left.Count) {
        Say "Still missing: $($left -join ', '). See the messages above."
        $progressText.Text = "Finished, but still missing: $($left -join ', ') - see the messages below."
    } else {
        Say $(if ($only) { 'Installed. Running your program...' } else { 'All tools are installed. Restart NppDev so IntelliSense picks them up.' })
        $progressBar.Value = 1000
        $progressText.Text = 'All done (100%)'
    }
    $btnCheck.Enabled = $true
    $left.Count -eq 0
}

$btnInstall.add_Click({ if (Install-Missing $null) { $script:exitCode = 0 } })
$btnCheck.add_Click({ $btnCheck.Enabled = $false; Say 'Checking...'; $script:report = Get-Report; Show-Report $report; Say 'Done.'; $btnCheck.Enabled = $true })
$btnClose.add_Click({ $form.Close() })
if ($wanted.Count) {                                 # from Run: start at once, close by itself when it worked
    $form.add_Shown({
        if (Install-Missing $wanted) {
            $script:exitCode = 0
            $t = New-Object System.Windows.Forms.Timer; $t.Interval = 1500
            $t.add_Tick({ $this.Stop(); $form.Close() }); $t.Start()
        }
    })
}

Show-Report $report
if ($report.python) { Say "Python: $($report.python)" }
if ((Get-Missing $report).Count -eq 0) { $script:exitCode = 0 }
[void]$form.ShowDialog()
exit $script:exitCode
