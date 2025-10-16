# sc4-imgui-advanced-lotplop

A DLL Plugin for SimCity 4 with advanced lot placement tools.

## System Requirements

* SimCity 4 version 641
* Windows 10 or later

## Installation

1. Close SimCity 4.
2. Copy `SC4AdvancedLotPlop.dll` into the Plugins folder in the SimCity 4 installation directory or Documents/SimCity 4 directory.
3. Start SimCity 4.

## Troubleshooting

The plugin writes a `SC4AdvancedLotPlop.log` file in the same folder as the plugin.

# License

This project is licensed under the terms of the GNU Lesser General Public License version 3.0.    
See [LICENSE.txt](LICENSE.txt) for more information.

## 3rd party code

[gzcom-dll](https://github.com/nsgomez/gzcom-dll/tree/master) Located in the vendor folder, MIT License.    
[spdlog](https://github.com/gabime/spdlog) Located in the vendor folder, MIT License.    
[args](https://github.com/Taywee/args) Located in the vendor folder, MIT License.    
[mINI](https://github.com/metayeti/mINI) Located in the vendor folder, MIT License.    
[Windows Implementation Library](https://github.com/microsoft/wil) - MIT License    
[sc4-dll-basics](https://github.com/0xC0000054/sc4-dll-basics) Located in the vendor folder, MIT License.    

# Source Code

## Prerequisites

* Visual Studio 2022 or CLion with Visual Studio toolchain
* CMake 3.20+
* Windows 10 or later

## Building the plugin

```bash
# Configure with CMake presets
cmake --preset vs2022-win32-debug     # Debug build
cmake --preset vs2022-win32-release   # Release build

# Build
cmake --build cmake-build-debug-visual-studio --config Debug
cmake --build cmake-build-release-visual-studio --config Release
```

The build system automatically deploys the DLL to your SimCity 4 Plugins folder.

## Debugging the plugin

Configure your IDE to launch SimCity 4 with the following command line:    
`-intro:off -CPUcount:1 -w -CustomResolution:enabled -r1920x1080x32`

You may need to adjust the window resolution for your primary screen.
