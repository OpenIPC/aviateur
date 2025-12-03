# Aviateur

<p style="text-align: center;">
  <a href="https://github.com/OpenIPC/aviateur">
    <img src="assets/logo.svg" width="100" alt="Aviateur logo">
  </a>
</p>

OpenIPC FPV ground station for Linux/Windows/macOS. Forked from [fpv4win](https://github.com/OpenIPC/fpv4win).

![](tutorials/interface.png)

> [!NOTE]
> No Adaptive Link support for Windows.

> [!NOTE]
> Only RTL8812AU Wi-Fi adapter is supported.

> [!NOTE]
> No MAVLink support.

### How to run on Windows

1. Install the GStreamer runtime (the development installer is not necessary)
   from [GStreamer](https://gstreamer.freedesktop.org/download/#windows).
2. Add `C:\Program Files\gstreamer\1.0\msvc_x86_64\bin` to your PATH environment variable.
3. Download [Zadig](https://zadig.akeo.ie/).
4. Install the libusb driver for your adapter.
   Go *Options* → *List All Devices*
   ![](tutorials/zadig1.jpg)
   Select your adapter. Install the driver. Remember the USB ID, we will need it soon.
   ![](tutorials/zadig2.jpg)
5. Run Aviateur.

### How to run on Linux

1. (Optional) Go to `/lib/udev/rules.d`, create a new file named `80-my8812au.rules` and add
   `SUBSYSTEM=="usb", ATTRS{idVendor}=="0bda", ATTRS{idProduct}=="8812", MODE="0666"` in it.
2. (Optional) Call `sudo udevadm control --reload-rules`, then reboot (this is definitely required).
3. Run Aviateur (if you skip step 1 & 2, root privileges are needed to access the adapter).

### How to run on macOS

1. Build yourself.
2. **Important**: Run the app from terminal to ensure proper environment variable handling:
   ```bash
   open ./build/bin/aviateur.app
   ```

Currently, I cannot find a way to distribute it on macOS. So, you have to build it yourself.

### Common run issues

* If the application crashes at startup on **Windows**,
  install [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170#latest-microsoft-visual-c-redistributable-version)
  first.

### Latency test

![](tutorials/latency_test.jpg)

> [!NOTE]
> Generally, enabling the GStreamer backend can achieve a lower glass-to-glass latency.

### TODOs

- Ground side OSD

### How to build on Windows

1. Install vcpkg somewhere else.
   ```powershell
   git clone https://github.com/microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```

2. Install dependencies.
   ```powershell
   .\vcpkg integrate install
   .\vcpkg install libusb ffmpeg libsodium opencv
   ```

3. Add VCPKG_ROOT to environment. (Change the value to your vcpkg path.)
   ![](tutorials/vcpkg.jpg)

4. Clone third-party library sources.
   ```powershell
   git submodule update --init --recursive
   ```

5. Install GStreamer (both the runtime and development installer)
   from [GStreamer](https://gstreamer.freedesktop.org/download/#windows). Add `C:\Program Files\gstreamer\1.0\msvc_x86_64\bin` to your PATH environment variable.

6. Build the project.
   ```bash
   mkdir build && cd build
   cmake ../
   make
   ```

### How to build on Linux (tested on Ubuntu 24.04)

1. Install dependencies.
   ```bash
   git submodule update --init --recursive
   sudo apt install libusb-1.0-0-dev ffmpeg libsodium-dev libopencv-dev xorg-dev libpcap-dev
   ```

2. Build the project.

### How to build on macOS

1. Install dependencies:

   Xcode: This assumes you already have Xcode installed and have run `xcode-select --install`

   [Homebrew](https://brew.sh/)

   [Vulkan](https://vulkan.lunarg.com/sdk/home)  - Install Location ~/ (default), no need to pick any extra options in
   installer (default)

   Extra Packages with Homebrew:
   ```bash
   brew install libusb ffmpeg libsodium opencv libpcap cmake
   ```

2. Add the following content to `YOUR_HOME/.zprofile` (change the sdk version and username to your own
   version/username).

   `nano ~/.zprofile`, paste the text in, `ctrl-o` to save, `ctrl-x` to exit.
   ```
   VULKAN_SDK="/Users/zzz/VulkanSDK/1.4.321.0/macOS"
   export VULKAN_SDK
   PATH="$PATH:$VULKAN_SDK/bin"
   export PATH
   DYLD_LIBRARY_PATH="$VULKAN_SDK/lib:${DYLD_LIBRARY_PATH:-}"
   export DYLD_LIBRARY_PATH
   VK_ADD_LAYER_PATH="$VULKAN_SDK/share/vulkan/explicit_layer.d"
   export VK_ADD_LAYER_PATH
   VK_ICD_FILENAMES="$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json"
   export VK_ICD_FILENAMES
   VK_DRIVER_FILES="$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json"
   export VK_DRIVER_FILES
   PKG_CONFIG_PATH="$VULKAN_SDK/lib/pkgconfig:$PKG_CONFIG_PATH"
   export PKG_CONFIG_PATH
   ```

3. Log out and in for the above change to take effect.

4. Build the project:
   ```bash
   git clone https://github.com/OpenIPC/aviateur
   cd aviateur
   git submodule update --init --recursive
   mkdir build && cd build
   cmake ../
   make
   ```

5. As noted above, you must run the file through terminal:

   `open ./bin/aviateur.app`

   Or if you want to see the log file while running:
   `./bin/aviateur.app/Contents/MacOS/aviateur`

### Common build issues

On Windows

```
CMake Error at C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/share/cmake-3.29/Modules/FindPackageHandleStandardArgs.cmake:230 (message): ...
```

This is because the pre-installed vcpkg from Visual Studio installer overrides the PKG_ROOT environment variable.
To fix this, find `set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")` in CMakeLists.txt,
replace `$ENV{VCPKG_ROOT}` with the vcpkg you cloned previously.
