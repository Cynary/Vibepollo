param([Parameter(Mandatory=$true)][string]$Launcher)
$ErrorActionPreference = 'Stop'
$Launcher = (Resolve-Path $Launcher).Path
$ready = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset,
    'Local\MoonmachineDirectCaptureReady')
try {
    # Run in an isolated session with no capture helper: this event belongs to
    # the live handshake, so this test must not run during streaming.
    if (Get-Process sunshine_wgc_capture -ErrorAction SilentlyContinue) {
        throw 'Close the stream before running the readiness test.'
    }
    $ready.Reset() | Out-Null
    $command = '"' + $env:ComSpec + '" /d /c exit 42'
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $child = Start-Process $Launcher -ArgumentList $command -PassThru
    $handle = $child.Handle
    if (!$child.WaitForExit(50000)) {
        Stop-Process -Id $child.Id
        throw 'Missing readiness did not time out.'
    }
    if ($child.ExitCode -ne 1460 -or $watch.ElapsedMilliseconds -lt 44000) {
        throw "Unexpected readiness timeout: $($child.ExitCode), $($watch.ElapsedMilliseconds) ms"
    }
    $ready.Set() | Out-Null
    $child = Start-Process $Launcher -ArgumentList $command -PassThru
    $handle = $child.Handle
    if (!$child.WaitForExit(5000)) {
        Stop-Process -Id $child.Id
        throw 'Ready launch did not finish.'
    }
    if ($child.ExitCode -ne 42) {
        throw "Child exit code changed: $($child.ExitCode)"
    }
    'PASS: readiness blocks until timeout; ready launch preserves child exit code.'
} finally {
    $ready.Dispose()
}
