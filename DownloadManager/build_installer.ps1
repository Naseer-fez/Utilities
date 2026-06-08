param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

Write-Host "Download Manager installer build"
Write-Host "Project: $Root"

if ($Clean) {
    foreach ($Name in @("build", "dist")) {
        $Target = Join-Path $Root $Name
        if (Test-Path $Target) {
            $Resolved = (Resolve-Path $Target).Path
            if (-not $Resolved.StartsWith($Root, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to clean outside the project folder: $Resolved"
            }
            Write-Host "Cleaning $Resolved"
            Remove-Item -LiteralPath $Resolved -Recurse -Force
        }
    }
}

Write-Host "Checking Python build dependencies..."
@'
missing = []
for package, import_name in [
    ("cx_Freeze", "cx_Freeze"),
    ("PySide6", "PySide6"),
    ("spotdl", "spotdl"),
    ("yt-dlp", "yt_dlp"),
]:
    try:
        __import__(import_name)
    except Exception:
        missing.append(package)
if missing:
    print("Missing packages: " + ", ".join(missing))
    print("Install them with:")
    print("python -m pip install " + " ".join(missing))
    raise SystemExit(1)
'@ | python -

if (-not (Test-Path (Join-Path $Root "icon.ico"))) {
    Write-Host "Creating icon.ico from icon.png..."
    @'
from pathlib import Path
from PIL import Image

source = Path("icon.png")
target = Path("icon.ico")
if not source.exists():
    raise SystemExit("icon.png not found")
image = Image.open(source)
image.save(target, sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
'@ | python -
}

Write-Host "Freezing app..."
python setup.py build_exe
if ($LASTEXITCODE -ne 0) {
    throw "cx_Freeze build_exe failed. Try running with the -Clean flag."
}

Write-Host "Packaging MSI..."
$MsiOk = $true
try {
    python setup.py bdist_msi --skip-build 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { $MsiOk = $false }
} catch {
    $MsiOk = $false
}

$MsiPath = Join-Path $Root "dist\DownloadManagerSetup-1.1.0-win64.msi"
if ($MsiOk -and (Test-Path $MsiPath)) {
    $MsiSize = (Get-Item $MsiPath).Length
    if ($MsiSize -lt 25MB) {
        Write-Host "WARNING: MSI file is suspiciously small ($MsiSize bytes), treating as failed."
        $MsiOk = $false
    }
}

if ($MsiOk -and (Test-Path $MsiPath)) {
    Write-Host ""
    Write-Host "MSI installer ready:"
    Write-Host $MsiPath
} else {
    Write-Host ""
    Write-Host "MSI packaging failed (known python-msilib FCI bug on Python 3.13+)."
    Write-Host "Falling back to portable ZIP distribution..."

    $BuildDir = Get-ChildItem -Path (Join-Path $Root "build") -Directory -Filter "exe.win-*" |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
    if (-not $BuildDir -or -not (Test-Path $BuildDir)) {
        throw "Frozen build directory not found under build\"
    }

    $DistDir = Join-Path $Root "dist"
    if (-not (Test-Path $DistDir)) { New-Item -ItemType Directory -Path $DistDir | Out-Null }

    $ZipPath = Join-Path $DistDir "DownloadManager-1.1.0-win64-portable.zip"
    if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }

    $SevenZip = Get-Command 7z -ErrorAction SilentlyContinue
    if ($SevenZip) {
        Write-Host "Compressing with 7-Zip..."
        & 7z a -tzip -mx=7 $ZipPath "$BuildDir\*"
        if ($LASTEXITCODE -ne 0) {
            throw "7-Zip compression failed."
        }
    } else {
        Write-Host "Compressing with PowerShell..."
        Compress-Archive -Path "$BuildDir\*" -DestinationPath $ZipPath -Force
    }

    if (-not (Test-Path $ZipPath)) {
        throw "ZIP was not created: $ZipPath"
    }

    Write-Host ""
    Write-Host "Portable ZIP installer ready:"
    Write-Host $ZipPath
    Write-Host ""
    Write-Host "To install: extract the ZIP to a folder and run DownloadManager.exe."
}
