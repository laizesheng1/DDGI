[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug"
)

$projectRoot = Split-Path -Parent $PSScriptRoot
$candidates = @(
    (Join-Path $projectRoot "bin\\$Configuration\\VK_DDGI.exe"),
    (Join-Path $projectRoot "bin\\VK_DDGI.exe")
)
$executable = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

if ($null -eq $executable) {
    throw "VK_DDGI.exe was not found. Build it first with tools/build.ps1 -Configuration $Configuration."
}

& $executable
exit $LASTEXITCODE
