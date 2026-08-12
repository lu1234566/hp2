param(
    [Parameter(Mandatory = $true)]
    [string]$GameDirectory
)

$ErrorActionPreference = "Stop"
$resolvedGameDirectory = (Resolve-Path $GameDirectory).Path
$remoteRoot = "/sdcard/Android/data/com.hp2.mobile/files/game"
$expectedDirectories = @("Maps", "Music", "Sounds", "System", "Textures")

if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw "adb was not found. Install Android platform-tools and add adb to PATH."
}

adb get-state | Out-Null
adb shell mkdir -p $remoteRoot

$copied = 0
foreach ($directory in $expectedDirectories) {
    $source = Join-Path $resolvedGameDirectory $directory
    if (Test-Path $source -PathType Container) {
        adb push $source "$remoteRoot/"
        $copied++
    }
}

if ($copied -eq 0) {
    throw "No HP2 data directories were found. Select the installed PC game directory, not the MDF root."
}

Write-Host "Game data installed in $remoteRoot"
Write-Host "Open HP2 Mobile and press Start to rescan packages."
