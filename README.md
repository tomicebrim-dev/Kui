# Kui
Kui is a time tracking application built in C++, its purpose is to keep you on track and track how much time you spend a day working, or wasting time. 

## Open-Source & Not Open Contribution
Kui is free and open-source, however we are not looking for contributors please refrain from opening pull requests.

## Building
Needs Windows 10+, Visual Studio 2022+ (Desktop C++ workload) and CMake 3.25+. Dependencies are vendored in `ThirdParty/`.

```bat
cmake --workflow --preset release
```

Or step by step: `cmake --preset vs`, `cmake --build --preset debug|release`, `ctest --preset debug|release`.
For VS Code / CLion / clangd use the `ninja` preset from a x64 Developer prompt (emits `compile_commands.json`).
Local overrides (e.g. signing) go in a gitignored `CMakeUserPresets.json`.

Output: `build\vs\Release\kui.exe` (one exe; `--tracker` runs it as the tray tracker).

## Installer (.msi)
Needs the WiX CLI (.NET SDK required):

```bat
dotnet tool install -g wix --version 5.0.2
wix extension add -g WixToolset.Util.wixext/5.0.2
cmake --build --preset msi
```

Output: `build\vs\Kui-<version>-x64.msi`. Per-user install, no admin prompt.

## Code signing
Optional. Put a code-signing certificate in your Windows certificate store, then configure with its SHA1 thumbprint; `kui.exe` and the `.msi` are signed with `signtool` (from the Windows SDK) as they're built:

```bat
cmake --preset vs -DKUI_SIGN_THUMBPRINT=<thumbprint>
```

`-DKUI_SIGN_TIMESTAMP=<url>` overrides the timestamp server (default DigiCert).


