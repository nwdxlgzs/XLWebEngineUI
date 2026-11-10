# Optional Microsoft D3DCompiler 47

Copyright Microsoft Corporation. All rights reserved.

This directory contains the **unmodified x64 redistributable** from Microsoft
Windows SDK 8.1, `Redist/D3D/x64/d3dcompiler_47.dll`, version 6.3.9600.16384.
The original Microsoft Authenticode signature is preserved. Its exact size and
SHA256, provenance and verification record are in `source.json`.

This proprietary Microsoft binary is distributed under the Windows SDK's
redistributable terms, separately from the licenses for the engine's source.
It imposes no requirement to publish the application's source code.
Microsoft documents application-local redistribution of D3DCompiler 46/47:
https://learn.microsoft.com/en-us/windows/win32/directx-sdk--august-2009-
section “Using DirectX SDK projects with Visual Studio”, step 6.

The SDK places this file **only beside the Windows engine DLL and WebTest**.
Do not install it into Windows system directories or modify its contents.
It is optional: applications may remove it. FXC is resolved lazily from the
engine image directory, then the system directory, using absolute paths.
If no usable FXC exists, affected candidates fail and rendering continues
through the remaining native OpenGL/ANGLE/Vulkan/SwiftShader/Raster chain.
Static DXC is independent of this file. Deleting the local copy does not
disable an independently installed system FXC.

The binary itself comes from the Windows 8.1 SDK redistribution directory.
Successful checks on a current Windows host are not Windows 7 certification.
