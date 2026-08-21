[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",
    [string]$BuildDirectory = "build",
    [switch]$ConfigureOnly
)

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $projectRoot $BuildDirectory
}

cmake -S $projectRoot -B $buildPath
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $ConfigureOnly) {
    cmake --build $buildPath --config $Configuration --target VK_DDGI
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
