# Building this Horion fork on modern Visual Studio

This repository is archived-era Horion source. The code targets an old Minecraft Bedrock build and uses the Visual Studio 2019 (`v142`) C++ toolset. A successful compile does **not** mean the resulting DLL is compatible with current Minecraft Bedrock versions.

## Recommended setup

1. Install Visual Studio Community with **Desktop development with C++**.
2. In Individual components, install **MSVC v142 - VS 2019 C++ x64/x86 build tools (v14.29)**.
3. Install a Windows 10/11 SDK.
4. Clone this repository rather than building from a ZIP.
5. Open `Horion.sln`.
6. Restore NuGet packages.
7. Select **Release | x64**.
8. Build the solution.

## NuGet dependencies

The Visual Studio project expects these packages from `packages.config`:

- `Microsoft.XAudio2.Redist` 1.2.0
- `directxtk_desktop_2017` 2020.2.24.4

If the build reports missing `.targets` files under `packages\`, use **Restore NuGet Packages** in Visual Studio and build again.

## Modern-build compatibility

The original `Horion.vcxproj` contains old machine-specific paths, including Visual Studio 2017 and a developer-local DirectX path. `Directory.Build.targets` in this fork overrides those x64 include/library paths using the active Visual Studio and Windows SDK variables, so you should not need to install Visual Studio 2017 or recreate somebody else's local `D:\ProgrammeX` folders.

## Important compatibility note

Horion hooks Minecraft Bedrock internals using signatures, offsets, SDK classes, and vtable assumptions. Those change as Minecraft updates. The current goal of the `modernize-build` branch is to make the historical source easier to compile with a modern development environment; updating it to a current Bedrock release is a separate reverse-engineering/compatibility task.
