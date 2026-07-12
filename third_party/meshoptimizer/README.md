# meshoptimizer subset

This directory contains the minimum source subset required by the host-only
`el2d_mesh_simplify` converter helper:

- `meshoptimizer.h`
- `allocator.cpp`
- `simplifier.cpp`
- `LICENSE.md`

Source: https://github.com/zeux/meshoptimizer/tree/v1.2

Pinned commit: `9d9890c73011d75920af614485296d1e03e95448`

License: MIT. Uses meshoptimizer. Copyright (c) 2016-2026, Arseny Kapoulkine.

This code is compiled only for the PC conversion tool. It is not linked into
the EL2D Lite embedded runtime or consuming firmware.
