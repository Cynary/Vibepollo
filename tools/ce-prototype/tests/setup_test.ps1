param([Parameter(Mandatory=$true)][string]$SetupScript)
$ErrorActionPreference='Stop'
$SetupScript=(Resolve-Path $SetupScript).Path
if(Get-Process sunshine_wgc_capture -ErrorAction SilentlyContinue){throw 'Close streaming before this test.'}
# Keep service control local to the fixture. Environment values are real per-user
# values, so preserve them even when an assertion fails.
$script:serviceRunning=$true
function Get-Service { param($Name) if($Name -ne 'ApolloService'){throw 'Unexpected service'};if($InstallDirectory -ne "$root\install"){throw 'Test attempted service control outside fixture'};[pscustomobject]@{Status=$(if($script:serviceRunning){'Running'}else{'Stopped'})} }
function Stop-Service { param($Name) $script:serviceRunning=$false }
function Start-Service { param($Name) $script:serviceRunning=$true }
function Assert($Condition,$Message){if(!$Condition){throw $Message}}
function Expect-Failure([scriptblock]$Action,[string]$Pattern){
    $message=$null
    try{& $Action | Out-Null}catch{$message=$_.Exception.Message}
    Assert ($message -and $message -like $Pattern) "Expected failure '$Pattern'; received '$message'"
}
$vars=@('MOONMACHINE_CE_TARGET_PATH','MOONMACHINE_CE_HOOK','MOONMACHINE_CE_TARGET_PID','MOONMACHINE_CE_VALIDATE_FINAL_OUTPUT')
$before=@{};foreach($v in $vars){$before[$v]=[Environment]::GetEnvironmentVariable($v,'User')}
$root=Join-Path $env:TEMP ('ce-setup-test-'+[Guid]::NewGuid().ToString('N'))
try{
    New-Item "$root\install\tools","$root\install\config" -ItemType Directory -Force | Out-Null
    [IO.File]::WriteAllText("$root\install\tools\sunshine_wgc_capture.exe",'original-helper')
    foreach($name in @('target.exe','hook.dll','candidate.exe','ce-stream-launcher.exe')){[IO.File]::WriteAllText("$root\$name",$name)}
    $cmd='"C:\Program Files\Buddy\MoonDeckStream.exe" --unchanged "argument with spaces"'
    $doc=@{apps=@(@{name='MoonDeckStream';cmd=$cmd},@{name='Other';cmd='original-other'});env=@{keep='value'}}
    $appFile="$root\install\config\apps.json"
    $doc|ConvertTo-Json -Depth 20|Set-Content $appFile
    $setupOptions=@{InstallDirectory="$root\install";StateDirectory="$root\state"}
    $enable=@{TargetPath="$root\target.exe";HookPath="$root\hook.dll";HelperPath="$root\candidate.exe";LauncherPath="$root\ce-stream-launcher.exe"}
    & $SetupScript -Mode Enable @setupOptions @enable | Out-Null
    Assert $script:serviceRunning 'Enable left service stopped'
    $live=Get-Content $appFile -Raw|ConvertFrom-Json
    Assert ($live.apps[0].cmd -eq ('"'+$enable.LauncherPath+'" '+$cmd)) 'Command or arguments changed'
    Expect-Failure {& $SetupScript -Mode Enable @setupOptions @enable} '*already exists*'
    $wrapped=$live.apps[0].cmd
    $live.apps[0].cmd='user-edited-command'
    $live|ConvertTo-Json -Depth 20|Set-Content $appFile
    Expect-Failure {& $SetupScript -Mode Disable @setupOptions} '*command changed*'
    $live.apps[0].cmd=$wrapped;$live.apps[1].cmd='new-unrelated-command'
    $live|ConvertTo-Json -Depth 20|Set-Content $appFile
    [IO.File]::WriteAllText("$root\install\tools\sunshine_wgc_capture.exe",'updated-by-someone-else')
    Expect-Failure {& $SetupScript -Mode Disable @setupOptions} '*helper changed*'
    Copy-Item "$root\candidate.exe" "$root\install\tools\sunshine_wgc_capture.exe" -Force
    & $SetupScript -Mode Disable @setupOptions | Out-Null
    $restored=Get-Content $appFile -Raw|ConvertFrom-Json
    Assert ($restored.apps[0].cmd -eq $cmd) 'Original command not restored'
    Assert ($restored.apps[1].cmd -eq 'new-unrelated-command') 'Unrelated edit lost'
    Assert ($restored.env.keep -eq 'value') 'Unrelated config lost'
    Assert ([IO.File]::ReadAllText("$root\install\tools\sunshine_wgc_capture.exe") -eq 'original-helper') 'Helper not restored'
    foreach($v in $vars){Assert ([Environment]::GetEnvironmentVariable($v,'User') -eq $before[$v]) "Environment not restored: $v"}
    Assert $script:serviceRunning 'Disable left service stopped'
    Assert (!(Test-Path "$root\state\state.json")) 'Active state left after disable'
    'PASS: enable/disable, command arguments, environment restore, duplicate setup rejection, conflict guards, unrelated edit preservation.'
}finally{
    foreach($v in $vars){[Environment]::SetEnvironmentVariable($v,$before[$v],'User')}
    if(Test-Path $root){Remove-Item $root -Recurse -Force}
}
