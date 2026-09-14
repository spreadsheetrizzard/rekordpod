param(
    [Parameter(Position = 0)]
    [string]$AssetDirectory = (Join-Path $PSScriptRoot "assets"),

    [Parameter(Position = 1)]
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "dist-release")
)

$ErrorActionPreference = "Stop"
$Python = Join-Path $PSScriptRoot ".venv-windows\Scripts\python.exe"
$env:PIP_CACHE_DIR = Join-Path $PSScriptRoot ".pip-cache"
$env:PYINSTALLER_CONFIG_DIR = Join-Path $PSScriptRoot ".pyinstaller"
$Required = @(
    "rekordpod-public-beta-1-ipod6g.zip"
)

foreach ($Archive in $Required) {
    $Path = Join-Path $AssetDirectory $Archive
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing $Path"
    }
}

if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    py -3.12 -m venv (Join-Path $PSScriptRoot ".venv-windows")
}

& $Python -m pip install --disable-pip-version-check `
    -r (Join-Path $PSScriptRoot "requirements-installer.txt")

Remove-Item -LiteralPath (Join-Path $PSScriptRoot "build") `
    -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $PSScriptRoot "dist") `
    -Recurse -Force -ErrorAction SilentlyContinue

$env:REKORDPOD_ASSET_DIR = (Resolve-Path -LiteralPath $AssetDirectory).Path
try {
    & $Python -m PyInstaller --noconfirm --clean `
        (Join-Path $PSScriptRoot "rekordpod-installer.spec")
}
finally {
    Remove-Item Env:\REKORDPOD_ASSET_DIR -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$Built = Join-Path $PSScriptRoot "dist\Rekordpod Installer.exe"
$Destination = Join-Path $OutputDirectory "Rekordpod Installer.exe"
Copy-Item -LiteralPath $Built -Destination $Destination -Force

$Hash = Get-FileHash -LiteralPath $Destination -Algorithm SHA256
$Hash | Format-List | Out-String | Set-Content `
    -LiteralPath (Join-Path $OutputDirectory "Rekordpod Installer SHA256.txt") `
    -Encoding UTF8

Write-Host "Built $Destination"
Write-Host "SHA-256 $($Hash.Hash)"
