<div align="center">
  <img src="https://i.imgur.com/4KbH9oV.png" width="400"></img>
</div>

<br>

<div align="center">
  <a href="http://www.jahshaka.com/">Website</a>
  &nbsp;&nbsp;&bull;&nbsp;&nbsp;
  <a href="https://www.x.com/jahshakafx">X Twitter</a>
  &nbsp;&nbsp;&bull;&nbsp;&nbsp;
  <a href="https://www.reddit.com/r/jahshaka/">Reddit</a>
  &nbsp;&nbsp;&bull;&nbsp;&nbsp;
  <a href="https://github.com/jahshaka/Studio/releases">Releases</a>
</div>

<br>

Jahshaka brings you the future of immersive digital content creation with the leading free and open source digital content creation suite.

![](https://i.imgur.com/Z7VZhGy.jpg)

## Features:
 - View scenes in virtual reality via the Oculus Quest/Quest Pro
 - Build and share your scenes with the world
 - Particle System
 - Animation Editor
 - Skeletal Animation
 - Realtime Dynamic Shadows
 - Powerful Extensible Material System
 - Asset Manager
 - Supports 4k displays
 - Custom Shaders and more&hellip;
 - Physics

## Screenshots

#### Particle System
![](https://i.imgur.com/XjXUnUx.gif)

#### Skeletal Animation
![](https://i.imgur.com/qTVhlPp.gif)

#### Shader format
![](https://i.imgur.com/sgaQpC8.png)

### Bullet Physics
![](https://giant.gfycat.com/TemptingRingedAsianwaterbuffalo.gif)

## Building From Source

Linux is the development platform (Ubuntu 26.04, GCC 15, Qt 6.10, an NVIDIA GPU with Vulkan 1.3+). The renderer is
Vulkan-only, on our fork of Ogre-Next (`github.com/jahshaka/ogre-next`). macOS builds through MoltenVK
(`docs/BUILDING_MACOS.md`); Windows is not yet built. The full guide with every dependency, the test suite and
troubleshooting is `docs/BUILDING_LINUX.md`.

### 1. Dependencies (Ubuntu — install ALL of them before configuring anything)

```bash
sudo apt-get install -y build-essential cmake ninja-build git python3 \
     qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qt6-svg-dev \
     qt6-httpserver-dev qt6-websockets-dev libqt6concurrent6 libqt6sql6-sqlite \
     libxrandr-dev libxaw7-dev rapidjson-dev libzzip-dev libsdl2-dev \
     glslang-tools spirv-tools vulkan-tools libshaderc-dev libfreeimage-dev \
     libxcb-randr0-dev libx11-xcb-dev libxcb1-dev libxcb-keysyms1-dev \
     libx11-dev libxt-dev libgl1-mesa-dev libglu1-mesa-dev libfreetype-dev \
     zlib1g-dev libvulkan-dev
```

Qt 6.10 or newer is required (`qt6-httpserver-dev` serves the in-app MCP endpoint). `libshaderc-dev` is the one
you must not miss: without it the engine configures "successfully" with no Vulkan renderer.

### 2. Clone

```bash
git clone --recursive https://github.com/jahshaka/Studio.git jahshaka
cd jahshaka
```

`--recursive` brings the IrisGL submodule, its vendored libraries, and the engine fork.

### 3. Build the engine (once per tree, ~10 minutes cold)

```bash
./irisgl/scripts/build-ogre.sh
```

Builds our fork of Ogre-Next at its pinned commit into `irisgl/thirdparty/ogre-next-install`. Run it again after a
pull that moved the engine pin (`git -C irisgl submodule update --init thirdparty/ogre-next` first).

### 4. Build and run Jahshaka

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DDISABLE_BREAKPAD=ON
cmake --build build-linux -j$(nproc)

cd build-linux/bin
./Jahshaka                              # the editor
./Jahshaka --engine-selftest out.png    # a 10-second proof the renderer works: exit 0 and four hash lines
```

Run the binary from `build-linux/bin` — media resolves from the binary's directory.

If you encounter any issues building, please open an issue.

## Credits
Royalty-free images from [Pixabay](https://pixabay.com/). Various icons sourced from [flaticon](http://www.flaticon.com/), [iconfinder](https://www.iconfinder.com/) under https://creativecommons.org/licenses/by/3.0/ and [the noun project](https://thenounproject.com/). Specific corresponding READMEs and licenses in their respective folders for free/open source assets used.

## License(GPLv3)
    http://www.jahshaka.com
    Copyright (c) 2016 Jahshaka LLC <coders@jahshaka.com>

    This is free software: you may copy, redistribute
    and/or modify it under the terms of the GPLv3 License

    For more information see the LICENSE file
