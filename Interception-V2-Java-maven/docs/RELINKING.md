# Corresponding source and relinking

The native Interception library uses LGPL-3.0-or-later. Its license and notices are included. `Interception-V2-corresponding-source.zip` contains the complete C ABI adapter and native SDK source with CMake build scripts. Extract, build with Windows x64 Visual Studio, and replace this package's native DLL with your rebuilt compatible DLL. Rust additionally includes the native import library. This package does not require a signature on the replacement DLL. The separate driver and its installation requirements are unchanged.
