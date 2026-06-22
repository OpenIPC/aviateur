# Set icon
# ----------------------------------------------------------
Write-Host "Setting icon..."

$fileName = "rcedit-x64.exe"
$downloadUrl = "https://github.com/electron/rcedit/releases/download/v2.0.0/$fileName"
$targetPath = Join-Path -Path $PWD.Path -ChildPath $fileName

if (-not (Test-Path -Path $targetPath -PathType Leaf))
{
    Write-Host "File $fileName does not exist, downloading from $downloadUrl ..." -ForegroundColor Cyan

    try
    {
        Invoke-WebRequest -Uri $downloadUrl -OutFile $targetPath -UseBasicParsing -ErrorAction Stop
        Write-Host "File $fileName downloaded, saved to $targetPath" -ForegroundColor Green
    }
    catch
    {
        Write-Error "Download failed! Error: $( $_.Exception.Message )"
        exit 1
    }
}
else
{
    Write-Host "File $fileName exists, download is not needed" -ForegroundColor Gray
}

./rcedit-x64.exe "cmake-build-release/bin/aviateur.exe" --set-icon "assets/logo.ico"

Write-Host "Set icon successfully!"
