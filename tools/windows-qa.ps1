# Run from PowerShell beside the portable game, or pass -GameFolder.
# All automated tests use a temporary profile folder; your normal saves are untouched.
param(
    [string]$GameFolder = $PSScriptRoot,
    [string]$Installer = "",
    [switch]$Install,
    [string]$ReportFolder = (Join-Path ([Environment]::GetFolderPath('Desktop')) ("OpenFirePower-QA-" + (Get-Date -Format "yyyyMMdd-HHmmss")))
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $ReportFolder | Out-Null
$ReportFolder = (Resolve-Path $ReportFolder).Path
Start-Transcript -Path (Join-Path $ReportFolder "session.txt") | Out-Null
$failed = $false
$results = @()
$oldPrefs = $env:OFP_TEST_PREF_PATH
try {
    Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,OSArchitecture | Format-List
    Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution | Format-List
    if ($Install) {
        if (!$Installer -or !(Test-Path $Installer)) { throw "Pass -Installer with the MSI file when using -Install." }
        $msi = (Resolve-Path $Installer).Path
        $p = Start-Process msiexec.exe -ArgumentList @('/i',('"' + $msi + '"'),'/L*v',('"' + (Join-Path $ReportFolder 'installer.log') + '"')) -PassThru -Wait
        if ($p.ExitCode -notin @(0,3010)) { throw "Installer returned $($p.ExitCode). See installer.log." }
    }
    $GameFolder = (Resolve-Path $GameFolder).Path
    $exe = Join-Path $GameFolder "openfirepower.exe"
    if (!(Test-Path $exe)) { throw "Game not found. Pass -GameFolder with the folder containing openfirepower.exe." }
    Get-FileHash $exe -Algorithm SHA256 | Format-List
    $env:OFP_TEST_PREF_PATH = (Join-Path $ReportFolder 'test-profile') + '\'
    New-Item -ItemType Directory -Force $env:OFP_TEST_PREF_PATH | Out-Null
    foreach ($test in @(@{Name='engine';Arg='--smoke';Success='smoke ok: 500 ticks'},@{Name='training';Arg='--smoke-training';Success='training ok'},@{Name='controller';Arg='--smoke-pad';Success='pad ok'})) {
        $stdout = Join-Path $ReportFolder ($test.Name + '.txt')
        $stderr = Join-Path $ReportFolder ($test.Name + '-errors.txt')
        Write-Host ("Running " + $test.Name + " test...")
        $ok = $true
        $p = Start-Process $exe -ArgumentList $test.Arg -WorkingDirectory $GameFolder -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
        if (!$p.WaitForExit(180000)) { $p.Kill(); $p.WaitForExit(); $ok=$false; Write-Warning ($test.Name + ' timed out.') }
        else { $p.WaitForExit(); $p.Refresh(); if ($p.ExitCode -ne 0) { $ok=$false; Write-Warning ($test.Name + ' failed: ' + $p.ExitCode) } }
        if (Test-Path $stdout) { Get-Content $stdout }
        if (Test-Path $stderr) { Get-Content $stderr }
        $logs = @($stdout,$stderr) | Where-Object { Test-Path $_ }
        if (Select-String -Path $logs -Pattern 'FAILED|DIDN.T' -Quiet) { $ok=$false }
        if (!(Select-String -Path $logs -SimpleMatch -Pattern $test.Success -Quiet)) { $ok=$false; Write-Warning 'Expected completion message was missing.' }
        if (!$ok) { $failed=$true }
        $results += [pscustomobject]@{Test=$test.Name;Passed=$ok;ExitCode=$p.ExitCode}
    }
    $results | Format-Table -AutoSize
    $results | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $ReportFolder 'results.json')
    Write-Host "Automated tests finished. Now follow WINDOWS-TESTS.md for fullscreen, real controllers, network play and installer checks."
    Write-Host "Reports: $ReportFolder"
    if ($failed) { Write-Warning "One or more automated checks failed. Send the report folder." }
} finally {
    $env:OFP_TEST_PREF_PATH = $oldPrefs
    Stop-Transcript | Out-Null
}
if ($failed) { exit 1 }
