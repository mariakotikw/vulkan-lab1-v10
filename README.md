# Лабораторная №1 — Vulkan, вариант 10

Текущая лабораторная №1, вариант 10, с заданиями 1–6: управление, сборка на Windows
и результаты проверки описаны в [LAB1.md](LAB1.md).

Два правильных додекаэдра используют общую геометрию и один graphics pipeline,
но разные descriptor sets и uniform buffers. В ImGui доступны две проекции,
преобразования, анимация и цвет каждого объекта.

![Два додекаэдра и меню ImGui](docs/screenshots/perspective.png)

## Сборка на Windows с MinGW

Нужны Git, CMake, MinGW с поддержкой C++20, `mingw32-make` и Vulkan SDK с `glslc`.
Каталоги компилятора, Make и `VULKAN_SDK/Bin` должны быть в `PATH`, а переменная
`VULKAN_SDK` должна указывать на установленный SDK. Для MinGW рекомендуется путь
к проекту без кириллицы. Команды выполняются из корня проекта:

```powershell
git clone https://github.com/mariakotikw/vulkan-lab1-v10.git
cd vulkan-lab1-v10
$env:VK_LAYER_PATH = Join-Path $env:VULKAN_SDK 'Bin'
cmake --preset mingw-debug
cmake --build build-debug --parallel 4
.\build-debug\vulkan-starter-app.exe
```

После изменений кода достаточно повторить сборку; для обычного запуска — последнюю
команду. Шейдеры компилируются автоматически. Проверки геометрии и состояния:

```powershell
ctest --test-dir build-debug --output-on-failure
```

Проверенная конфигурация: GCC 14.1.0, Vulkan SDK 1.4.363.0. Результаты проверки:
CTest 2/2 PASS, UI 62/62 PASS, Validation Layers 0 ERROR / 0 WARNING, exit code 0.
Локальная папка `.tools/` и результаты сборки в репозиторий не входят.

Основа — [vulkan-starter-app преподавателя](https://github.com/vladeemerr/vulkan-starter-app),
лицензия Apache-2.0; исходные `LICENSE` и `NOTICE` сохранены.
Ниже сохранена инструкция автора starter.

## Getting started

You need C++ compiler, Vulkan SDK and CMake installed before you can build this project.

This project uses C++20 standard and thus requires either of those compilers:
- GCC 10.X
- Clang 10
- Microsoft Visual Studio 2019

This is officially tested on *Windows* and *GNU/Linux platforms*, no *macOS* support yet.
If you have a working macOS solution of this code, consider submitting a PR so others
can build this example code without a hassle!

<ins>**1. Downloading the repository**</ins>

Start by cloning the repository with `git clone --depth 1 https://github.com/vladeemerr/vulkan-starter-app`

This repository does not contain any submodules, it utilizes CMake's `FetchContent` feature instead.

<ins>**2. Configuring the project**</ins>

Run either one of the CMake lines to download dependencies and configure the project:

```bash
cmake --preset debug       # for GNU/Linux (GCC/Clang)
cmake --preset msvc-debug  # for Windows (Visual Studio 2019)
cmake --preset mingw-debug # for Windows (MinGW)
```

If you wish to build in `release` mode, change `debug` to `release`.

If changes are made (added/removed files), or if you want to regenerate project files, rerun the command above.

<ins>**3. Building**</ins>

To build the project, use the line below. You are most likely using `debug` preset, so
the directory that will eventually contain your build files is named `build-debug`.

Likewise for `release` that directory will be named `build-release`

Run one those commands, depending on which preset you chose:

```bash
cmake --build build-debug --parallel # for debug
cmake --build build-release --parallel # for release
```

### Running

`build-*` directory will contain the executable in one of the subdirectories after successful build.

For `msvc-{debug|release}` builds output subdirectory is set to `Debug` or `Release` respectively.
For other configurations output subdirectory is set to `vulkan-starter-app`.

**Make sure your working directory is set to the project root!**
Project root is where this README file resides. Otherwise, the
code responsible for loading shaders or other resources from files will fail,
because relative paths are used.

### Compiling shaders

`CMakeLists.txt` has a build recipe for compiling shader files
along with an application. Look for a comment in this file to see
how to compile your shaders.
