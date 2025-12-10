# Copy GStreamer dlls
# ----------------------------------------------------------
$sourcePath = "C:\Program Files\gstreamer\1.0\msvc_x86_64\bin"
$destinationPath = "./cmake-build-release/bin"
$excludeFiles = @("cairo-2.dll", "cairo-gobject-2.dll", "cairo-script-interpreter-2.dll", "gstd3dshader-1.0-0.dll", "gstwebrtc-1.0-0.dll", "gstwebrtcnice-1.0-0.dll", "nice-10.dll", "opencore-amrnb-0.dll", "opencore-amrwb-0.dll", "soup-3.0-0.dll", "sqlite3-0.dll", "SvtAv1Enc.dll", "rsvg-2-2.dll", "libsrt.dll", "libssl-3-x64.dll", "libstdc++-6.dll", "libcrypto-3-x64.dll", "libspandsp-2.dll")

if (-not (Test-Path $destinationPath))
{
    New-Item -ItemType Directory -Path $destinationPath
}

# Get all dll files
Get-ChildItem -Path $sourcePath -Filter "*.dll" -File | Where-Object { $excludeFiles -notcontains $_.Name } | ForEach-Object {
    $sourceFilePath = $_.FullName
    $destinationFilePath = Join-Path $destinationPath $_.Name
    Copy-Item -Path $sourceFilePath -Destination $destinationFilePath -Exclude $excludeFiles
}

# Copy GStreamer plugin dlls
# ----------------------------------------------------------
$sourcePathPlugin = "C:\Program Files\gstreamer\1.0\msvc_x86_64\lib\gstreamer-1.0"
$destinationPathPlugin = "./cmake-build-release/bin/gstreamer-1.0"
$excludeFilesPlugin = @("gstwebview2.dll", "gstvpx.dll", "gstrav1e.dll", "gstquinn.dll", "gstelevenlabs.dll", "gstsvtav1.dll", "gstsrt.dll", "gstrsvg.dll", "gstwebrtc.dll", "gstwebrtcdsp.dll", "gstwebrtchttp.dll", "gstrswebrtc.dll", "gstpython.dll", "gstaws.dll")

if (-not (Test-Path $destinationPathPlugin))
{
    New-Item -ItemType Directory -Path $destinationPathPlugin
}

# Get all dll files
Get-ChildItem -Path $sourcePathPlugin -Filter "*.dll" -File | Where-Object { $excludeFilesPlugin -notcontains $_.Name } | ForEach-Object {
    $sourceFilePath = $_.FullName
    $destinationFilePath = Join-Path $destinationPathPlugin $_.Name
    Copy-Item -Path $sourceFilePath -Destination $destinationFilePath -Exclude $excludeFiles
}

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
