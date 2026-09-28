[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Enable','Disable','Status')][string]$Mode,
    [string]$TargetPath,
    [string]$HookPath,
    [string]$HelperPath,
    [string]$LauncherPath,
    [string]$InstallDirectory = "$env:ProgramFiles\Apollo",
    [string]$ApplicationName = 'MoonDeckStream',
    [string]$StateDirectory = "$env:LOCALAPPDATA\Moonmachine\DirectCapture"
)
$ErrorActionPreference = 'Stop'
$stateFile = Join-Path $StateDirectory 'state.json'
$appsFile = Join-Path $InstallDirectory 'config\apps.json'
$installedHelper = Join-Path $InstallDirectory 'tools\sunshine_wgc_capture.exe'
$variables = @('MOONMACHINE_CE_TARGET_PATH','MOONMACHINE_CE_HOOK',
    'MOONMACHINE_CE_TARGET_PID','MOONMACHINE_CE_VALIDATE_FINAL_OUTPUT')
function Write-JsonFile($Path, $Value) {
    $temporary = "$Path.new"
    [IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth 100),
        [Text.UTF8Encoding]::new($false))
    Move-Item $temporary $Path -Force
}
function Find-App($Document, $Name) {
    $matches = @($Document.apps | Where-Object { $_.name -eq $Name })
    if ($matches.Count -ne 1) { throw "Expected exactly one application named '$Name'." }
    return $matches[0]
}
function Restore-Environment($Saved) {
    foreach ($name in $variables) {
        [Environment]::SetEnvironmentVariable($name, $Saved.$name, 'User')
    }
}
if ($Mode -eq 'Status') {
    if (Test-Path $stateFile) {
        Get-Content $stateFile -Raw | ConvertFrom-Json |
            Select-Object TargetPath, ApplicationName, InstallDirectory, Enabled
    } else { 'Direct capture is not enabled by this script.' }
    return
}
if (Get-Process sunshine_wgc_capture -ErrorAction SilentlyContinue) {
    throw 'Close the stream before changing the capture helper.'
}
$serviceWasRunning = (Get-Service ApolloService).Status -eq 'Running'
if ($Mode -eq 'Disable') {
    if (!(Test-Path $stateFile)) { throw 'No saved setup exists; nothing was changed.' }
    $state = Get-Content $stateFile -Raw | ConvertFrom-Json
    if ($state.InstallDirectory -ne $InstallDirectory) { throw 'The saved installation path differs.' }
    $apps = Get-Content $appsFile -Raw | ConvertFrom-Json
    $app = Find-App $apps $state.ApplicationName
    if ($app.cmd -ne $state.EnabledCommand -and $app.cmd -ne $state.OriginalCommand) {
        throw 'The application command changed after setup. Restore it manually before disabling.'
    }
    $currentHash = (Get-FileHash $installedHelper).Hash
    if ($currentHash -ne $state.EnabledHelperHash -and $currentHash -ne $state.OriginalHelperHash) {
        throw 'The capture helper changed after setup. Refusing to overwrite a newer installation.'
    }
    if ((Get-FileHash "$StateDirectory\original-helper.exe").Hash -ne $state.OriginalHelperHash) {
        throw 'The saved helper failed its integrity check.'
    }
    Stop-Service ApolloService
    try {
        Copy-Item "$StateDirectory\original-helper.exe" $installedHelper -Force
        $app.cmd = $state.OriginalCommand
        Write-JsonFile $appsFile $apps
        Restore-Environment $state.Environment
        Remove-Item $stateFile
    } finally { if ($serviceWasRunning) { Start-Service ApolloService } }
    'Original capture helper, application command and environment restored.'
    return
}
if (Test-Path $stateFile) { throw 'A saved setup already exists. Disable it before selecting another game.' }
foreach ($value in @($TargetPath,$HookPath,$HelperPath,$LauncherPath)) {
    if (!$value -or !(Test-Path -LiteralPath $value -PathType Leaf)) {
        throw 'Enable requires existing TargetPath, HookPath, HelperPath and LauncherPath files.'
    }
}
$TargetPath = (Resolve-Path -LiteralPath $TargetPath).Path
$HookPath = (Resolve-Path -LiteralPath $HookPath).Path
$HelperPath = (Resolve-Path -LiteralPath $HelperPath).Path
$LauncherPath = (Resolve-Path -LiteralPath $LauncherPath).Path
if ($HelperPath -eq $installedHelper) { throw 'Use a separate candidate helper, not the installed helper.' }
$apps = Get-Content $appsFile -Raw | ConvertFrom-Json
$app = Find-App $apps $ApplicationName
if (!$app.cmd -or $app.cmd.Contains('ce-stream-launcher.exe')) {
    throw 'The application must have its original, nonempty launch command.'
}
New-Item $StateDirectory -ItemType Directory -Force | Out-Null
Copy-Item $installedHelper "$StateDirectory\original-helper.exe" -Force
$environment = @{}
foreach ($name in $variables) { $environment[$name] = [Environment]::GetEnvironmentVariable($name,'User') }
$state = [ordered]@{
    Version = 1; Enabled = $true; TargetPath = $TargetPath
    InstallDirectory = $InstallDirectory; ApplicationName = $ApplicationName
    OriginalCommand = $app.cmd; EnabledCommand = '"' + $LauncherPath + '" ' + $app.cmd
    OriginalHelperHash = (Get-FileHash $installedHelper).Hash
    EnabledHelperHash = (Get-FileHash $HelperPath).Hash
    Environment = $environment
}
# Save recovery information before changing the installed files.
Write-JsonFile $stateFile $state
Stop-Service ApolloService
try {
    Copy-Item $HelperPath $installedHelper -Force
    $app.cmd = $state.EnabledCommand
    Write-JsonFile $appsFile $apps
    [Environment]::SetEnvironmentVariable('MOONMACHINE_CE_TARGET_PATH',$TargetPath,'User')
    [Environment]::SetEnvironmentVariable('MOONMACHINE_CE_HOOK',$HookPath,'User')
    [Environment]::SetEnvironmentVariable('MOONMACHINE_CE_TARGET_PID',$null,'User')
    [Environment]::SetEnvironmentVariable('MOONMACHINE_CE_VALIDATE_FINAL_OUTPUT',$null,'User')
} catch {
    Copy-Item "$StateDirectory\original-helper.exe" $installedHelper -Force
    $app.cmd = $state.OriginalCommand
    Write-JsonFile $appsFile $apps
    Restore-Environment ([pscustomobject]$environment)
    Remove-Item $stateFile
    throw
} finally { if ($serviceWasRunning) { Start-Service ApolloService } }
"Direct capture enabled for $TargetPath. Launch it through $ApplicationName."
