# PurismCore Backend

This optional backend boundary checks whether a local PurismCore checkout can be built by the host or ESP-IDF Xtensa toolchain. A production extractor must additionally compare model evaluation output against a trusted local reference.

No PurismCore source is vendored. The default build and converter do not require it.

## Host Probe

Use a local PurismCore source checkout without committing it:

```powershell
cmake -S . -B build-purism -DEL2D_PURISMCORE_DIR=local/purismcore
cmake --build build-purism
build-purism\Debug\el2d_purism_probe.exe
```

The default `build` does not require PurismCore.

On Windows, the default Visual Studio C compiler cannot compile the current
PurismCore source checkout because that code uses C99 variable-length arrays.
Use MinGW, the ESP-IDF Xtensa GCC path, or a `PurismCoreBundle.h` release
artifact for the actual Core probe.

## ESP32-S3 Compile Note

The ESP-IDF 5.4.4 Xtensa GCC on this machine can compile the current
PurismCore source checkout with:

```text
-std=c99 -Wno-incompatible-pointer-types
```

The suppression is needed because `psm__i32` resolves to `long int` on the
Xtensa toolchain while the Cubism-compatible public API exposes several arrays
as `const int *`. Both are 32-bit on this target, but the type mismatch must be
handled deliberately in the ESP-IDF component rather than hidden in application
code.
