Remove-Item -Path "generated" -Recurse

New-Item -Path "generated" -ItemType Directory

New-Variable -Name "GENERATOR" -Visibility Public -Value "./pathfinder_shader_generator.exe"

# Compile shaders.
& $GENERATOR -i yuv.vert -o generated/yuv_vert.shdbin -t vert
& $GENERATOR -i yuv.frag -o generated/yuv_frag.shdbin -t frag

Set-Location "generated"

# Generate headers.
python ../convert_files_to_header.py shdbin

# Remove intermediate files.
Get-ChildItem -Recurse -File | Where { ($_.Extension -ne ".h") } | Remove-Item

# Wait for input.
Write-Host "All jobs finished."
$Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
