#!/bin/bash
set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}Building Aviateur for macOS...${NC}"

rm -rf build-macos
mkdir -p build-macos
cd build-macos

INSTALL_DIR="$(pwd)/dist"

echo -e "${GREEN}Configuring CMake...${NC}"
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 \
    -DCMAKE_OSX_ARCHITECTURES="arm64"

echo -e "${GREEN}Compiling...${NC}"
cmake --build . --config Release -j"$(sysctl -n hw.ncpu)"

echo -e "${GREEN}Installing to bundle...${NC}"
cmake --install .

APP_BUNDLE="$INSTALL_DIR/aviateur.app"

echo -e "${GREEN}Checking bundle structure...${NC}"
ls -la "$APP_BUNDLE/Contents/"
ls -la "$APP_BUNDLE/Contents/MacOS/"
ls -la "$APP_BUNDLE/Contents/Resources/"

echo -e "${GREEN}Fixing up bundle dependencies...${NC}"
if [ -x "$(command -v otool)" ] && [ -x "$(command -v install_name_tool)" ]; then
    EXECUTABLE="$APP_BUNDLE/Contents/MacOS/aviateur"
    
    echo "Checking dependencies for: $EXECUTABLE"
    otool -L "$EXECUTABLE"
    
    echo -e "${GREEN}Creating Frameworks directory...${NC}"
    mkdir -p "$APP_BUNDLE/Contents/Frameworks"
    
    FIXED_DEPS=()
    
    while IFS= read -r line; do
        DEP_PATH=$(echo "$line" | awk '{print $1}')
        
        if [[ "$DEP_PATH" == /usr/local/lib/* || "$DEP_PATH" == /opt/homebrew/lib/* ]]; then
            LIB_NAME=$(basename "$DEP_PATH")
            
            if [[ ! " ${FIXED_DEPS[@]} " =~ " ${LIB_NAME} " ]]; then
                echo -e "${GREEN}Copying dependency: $DEP_PATH${NC}"
                cp "$DEP_PATH" "$APP_BUNDLE/Contents/Frameworks/"
                
                NEW_ID="@rpath/$LIB_NAME"
                install_name_tool -id "$NEW_ID" "$APP_BUNDLE/Contents/Frameworks/$LIB_NAME"
                
                echo -e "${GREEN}Updating executable reference: $LIB_NAME${NC}"
                install_name_tool -change "$DEP_PATH" "$NEW_ID" "$EXECUTABLE"
                
                FIXED_DEPS+=("$LIB_NAME")
            fi
        fi
    done < <(otool -L "$EXECUTABLE" | tail -n +2)
    
    echo -e "${GREEN}Setting rpath in executable...${NC}"
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$EXECUTABLE"
else
    echo -e "${RED}Warning: otool or install_name_tool not found, skipping dependency fixup${NC}"
fi

echo -e "${GREEN}Verifying final dependencies...${NC}"
otool -L "$EXECUTABLE"

echo -e "${GREEN}Creating DMG manually...${NC}"
DMG_DIR="$(pwd)/dmg-temp"
rm -rf "$DMG_DIR"
mkdir -p "$DMG_DIR"

cp -r "$APP_BUNDLE" "$DMG_DIR/"

ln -s /Applications "$DMG_DIR/Applications"

DMG_FILE="Aviateur-macOS.dmg"
DMG_SIZE=$(du -sk "$DMG_DIR" | awk '{print $1}')
DMG_SIZE=$((DMG_SIZE + 20480))

echo -e "${GREEN}Creating DMG with size ${DMG_SIZE}KB...${NC}"
hdiutil create -volname "Aviateur" \
    -srcfolder "$DMG_DIR" \
    -ov \
    -format UDZO \
    -fs "HFS+" \
    -size "${DMG_SIZE}k" \
    "$DMG_FILE"

if [ -f "$DMG_FILE" ]; then
    echo -e "${GREEN}Done! DMG created at: $(pwd)/$DMG_FILE${NC}"
else
    echo -e "${RED}Error: DMG file not created${NC}"
    exit 1
fi

rm -rf "$DMG_DIR"
