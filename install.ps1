$InstallDir = "$env:LOCALAPPDATA\ShellLite"
$ExePath = Join-Path $PSScriptRoot "shlcpp.exe"
if (-not (Test-Path $ExePath)) {
    if (Test-Path (Join-Path $PSScriptRoot "build_cpp\Release\shell_lite_exec.exe")) {
        $ExePath = Join-Path $PSScriptRoot "build_cpp\Release\shell_lite_exec.exe"
    } elseif (Test-Path (Join-Path $PSScriptRoot "build_cpp\Release\shlcpp.exe")) {
        $ExePath = Join-Path $PSScriptRoot "build_cpp\Release\shlcpp.exe"
    } else {
        Write-Error "shlcpp.exe not found. Please place or build shlcpp.exe first."
        exit 1
    }
}
Write-Host "Installing ShellLite to $InstallDir..."
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir | Out-Null
}
Copy-Item $ExePath -Destination (Join-Path $InstallDir "shlcpp.exe") -Force
Write-Host "ShellLite binary copied."

$DllPath = Join-Path $PSScriptRoot "shell_lite_lib.dll"
if (-not (Test-Path $DllPath) -and (Test-Path (Join-Path $PSScriptRoot "build_cpp\Release\shell_lite_lib.dll"))) {
    $DllPath = Join-Path $PSScriptRoot "build_cpp\Release\shell_lite_lib.dll"
}
if (Test-Path $DllPath) {
    Copy-Item $DllPath -Destination $InstallDir -Force
    Write-Host "ShellLite shared library copied."
}

$StdLibPath = Join-Path $PSScriptRoot "shell_lite\stdlib"
if (Test-Path $StdLibPath) {
    $DestStdLib = Join-Path $InstallDir "stdlib"
    if (Test-Path $DestStdLib) {
        Remove-Item $DestStdLib -Recurse -Force
    }
    Copy-Item -Path $StdLibPath -Destination $InstallDir -Recurse -Force
    Write-Host "ShellLite standard library copied."
}
$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
$PathEntries = if ($UserPath) { ($UserPath -split ';') | Where-Object { $_ } } else { @() }
if ($PathEntries -notcontains $InstallDir) {
    Write-Host "Adding $InstallDir to User PATH..."
    $NewPath = if ($UserPath) { "$UserPath;$InstallDir" } else { $InstallDir }
    [Environment]::SetEnvironmentVariable("Path", $NewPath, "User")
    Write-Host "PATH updated. Please restart your terminal."
} else {
    Write-Host "ShellLite is already in your PATH."
}

$UserPathext = [Environment]::GetEnvironmentVariable("PATHEXT", "User")
$PathextEntries = if ($UserPathext) { ($UserPathext -split ';') | Where-Object { $_ } } else { @() }
if ($PathextEntries -notcontains ".SHL") {
    $NewPathext = if ($UserPathext) { "$UserPathext;.SHL" } else { ".SHL" }
    [Environment]::SetEnvironmentVariable("PATHEXT", $NewPathext, "User")
}

Write-Host "`nInstallation complete! You can now run 'shlcpp' from any terminal." -ForegroundColor Green