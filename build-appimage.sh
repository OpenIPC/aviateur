mkdir appimage-build-release
cd appimage-build-release

cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DAVIATEUR_ENABLE_GSTREAMER=ON

make -j$(nproc)

make install DESTDIR=AppDir

FILE="linuxdeploy-x86_64.AppImage"

if [ -f "$FILE" ]; then
#    rm $FILE
    echo "The file '$FILE' exists."
else
    echo "The file '$FILE' does not exist. Downloading..."
    wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fi

# Make it executable
chmod +x linuxdeploy*.AppImage

# The AppDir/share directory will not be created, we need to run this once to create it
./linuxdeploy-x86_64.AppImage --appdir AppDir

# Hook GStreamer plugins
bash ../linuxdeploy-plugin-gstreamer.sh

# Copy icon, binary & assets
cp ./bin/assets/logo.png ./AppDir/usr/share/icons/hicolor/128x128/apps/aviateur.png
cp -r ./bin/assets ./AppDir
cp ./bin/aviateur ./AppDir/usr/bin/

./linuxdeploy-x86_64.AppImage --appdir AppDir --output appimage -i ../assets/logo.png -d ../aviateur.desktop
