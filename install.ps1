$InstallDir = "$env:LOCALAPPDATA\ShellLite"
$ExePath = Join-Path $PSScriptRoot "shlcpp.exe"
if (-not (Test-Path $ExePath)) {
    Write-Error "shlcpp.exe not found in current directory. Please run this after building."
    exit
}
Write-Host "Installing ShellLite to $InstallDir..."
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir
}
Copy-Item $ExePath -Destination $InstallDir -Force
Write-Host "ShellLite binary copied."
$StdLibPath = Join-Path $PSScriptRoot "shell_lite\stdlib"
if (Test-Path $StdLibPath) {
    Copy-Item -Path $StdLibPath -Destination $InstallDir -Recurse -Force
    Write-Host "ShellLite standard library copied."
}
$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($UserPath -notlike "*$InstallDir*") {
    Write-Host "Adding $InstallDir to User PATH..."
    [Environment]::SetEnvironmentVariable("Path", $UserPath + ";" + $InstallDir, "User")
    Write-Host "PATH updated. Please restart your terminal."
} else {
    Write-Host "ShellLite is already in your PATH."
}
Write-Host "`nInstallation complete! You can now run 'shlcpp' from any terminal." -ForegroundColor Green