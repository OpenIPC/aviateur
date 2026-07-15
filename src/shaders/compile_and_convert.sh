#!/bin/bash

# Remove the 'generated' directory if it exists and create a fresh one
rm -rf generated
mkdir generated

# Define the generator path
GENERATOR="./pathfinder_shader_generator"

# Compile shaders
$GENERATOR -i yuv.vert -o generated/yuv_vert.shdbin -t vert
$GENERATOR -i yuv.frag -o generated/yuv_frag.shdbin -t frag

# Change into the generated directory
cd generated || exit

# Generate headers using the Python script
python3 ../convert_files_to_header.py shdbin

# Remove intermediate files (everything that is not a .h file)
find . -type f ! -name "*.h" -delete

# Wait for user input
echo "All jobs finished."
read -p "Press any key to continue..." -n1 -s