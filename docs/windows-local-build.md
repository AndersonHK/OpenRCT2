# Windows local build setup

This repo now has a repeatable Windows build path for this machine:

```powershell
.\scripts\windows\build-local.ps1
```

If PowerShell execution policy blocks direct script execution, use:

```cmd
scripts\windows\build-local.cmd
```

The wrapper runs:

```powershell
msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207
```

Use `-Configuration Debug` only when debugging or when a debug-only test session is needed. A Debug build is much slower in live play and can make even an empty park feel laggy.

To create the local mod deployment, use:

```cmd
scripts\windows\deploy-local.cmd
```

This builds Release by default, copies the fork-built executable to `D:\Games\Independent\OpenRCT2Mod`, and mirrors the fork-built `bin\data` directory into the mod deployment. That matters because this branch tracks `develop`, while `D:\Games\Independent\OpenRCT2` is a vanilla main-branch deployment with a different `data` payload.

## Rationale

The repo already owns dependency download and asset setup through `openrct2.proj`. On this machine, `cmake`, `ninja`, and `vcpkg` are not on `PATH`, but Visual Studio 2022 Community and MSBuild are installed. The project file is therefore the lowest-friction build entry point.

The default toolset selected by MSBuild was MSVC `14.38.33130`. It compiled most of the tree but failed to link against the downloaded prebuilt dependency libraries with unresolved standard-library symbols. MSVC `14.44.35207` is installed locally and links those same dependency libraries successfully, so the helper pins that toolset.

`/nr:false` avoids leaving new MSBuild worker nodes around after the build. Existing idle nodes from earlier builds may still exist, but the helper does not create more reusable nodes.

## Files touched

- `scripts/windows/build-local.ps1`: one-command local build wrapper.
- `scripts/windows/build-local.cmd`: execution-policy-friendly wrapper for the PowerShell script.
- `scripts/windows/deploy-local.ps1`: Release deployment wrapper for `D:\Games\Independent\OpenRCT2Mod`.
- `scripts/windows/deploy-local.cmd`: execution-policy-friendly wrapper for the deployment script.
- `src/openrct2-ui/scripting/ScWidget.hpp`: casts QuickJS function-list counts to the `int32_t` API type required under `/WX`.
- `src/openrct2/scripting/bindings/object/ScObject.hpp`: same QuickJS count casts.
- `src/openrct2/ride/Ride.cpp`: casts the vehicle-colour array bound passed to `std::clamp<int32_t>`.

## Verification

The following command succeeded and produced `bin/openrct2.exe`, `bin/openrct2-cli.exe`, and `bin/tests.exe`:

```powershell
msbuild openrct2.proj /m /nr:false /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207
```

MSBuild still reports one non-fatal Roslyn `System.Memory` binding warning from `openrct2.proj`.
