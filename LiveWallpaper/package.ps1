$ErrorActionPreference = 'Stop'

$ProjectDir = $PSScriptRoot
$ReleaseDir = Join-Path -Path $ProjectDir -ChildPath "ReleasePackage"
$ZipFile = Join-Path -Path $ProjectDir -ChildPath "LiveWallpaper_Release.zip"

Write-Host "Creating Release folder..."
if (Test-Path $ReleaseDir) {
    Remove-Item -Path $ReleaseDir -Recurse -Force
}
New-Item -Path $ReleaseDir -ItemType Directory | Out-Null

Write-Host "Copying executable and DLL..."
Copy-Item -Path (Join-Path $ProjectDir "LiveWallpaper.exe") -Destination $ReleaseDir
Copy-Item -Path (Join-Path $ProjectDir "live_wallpaper_rust.dll") -Destination $ReleaseDir

Write-Host "Copying shaders (if any exist)..."
$hlslFiles = Get-ChildItem -Path $ProjectDir -Filter "*.hlsl" -File
if ($hlslFiles.Count -gt 0) {
    foreach ($file in $hlslFiles) {
        Copy-Item -Path $file.FullName -Destination $ReleaseDir
    }
}

Write-Host "Copying README..."
if (Test-Path (Join-Path $ProjectDir "README.md")) {
    Copy-Item -Path (Join-Path $ProjectDir "README.md") -Destination $ReleaseDir
}

Write-Host "Zipping the package..."
if (Test-Path $ZipFile) {
    Remove-Item -Path $ZipFile -Force
}
Compress-Archive -Path "$ReleaseDir\*" -DestinationPath $ZipFile

Write-Host "Cleaning up Release folder..."
Remove-Item -Path $ReleaseDir -Recurse -Force

Write-Host "Packaging complete! Distribute the following file:"
Write-Host $ZipFile
