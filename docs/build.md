# Build steps (First time)

Replace `BUILD_TYPE` with `Release` or `Debug` depending on your requirements. Use `System` when building on the machine you will run the server on.

```bash
git clone https://github.com/KJNeko/IDHAN.git --recursive
cmake -DCMAKE_BUILD_TYPE=System -B build
cmake --build build -j$(nproc) --target IDHANServer
```

To also build the HydrusImporter:

```bash
cmake --build build -j$(nproc) --target IDHANServer HydrusImporter
```

## Targeting a different CPU

`Release` and `System` compile with `-march=$FGL_MARCH`, which defaults to `native`: the build targets whatever the
machine running the compiler supports. That is what you want when the build machine and the server are the same machine,
and wrong as soon as they are not, since the binary will fail with SIGILL on anything older.

To build something portable, name a level instead:

```bash
cmake -DCMAKE_BUILD_TYPE=Release -DFGL_MARCH=x86-64 -B build     # runs on any 64-bit x86
cmake -DCMAKE_BUILD_TYPE=Release -DFGL_MARCH=x86-64-v2 -B build  # Nehalem (2008) and later
cmake -DCMAKE_BUILD_TYPE=Release -DFGL_MARCH=x86-64-v3 -B build  # Haswell (2013) and later
```

`-DFGL_MARCH=` (empty) omits `-march` entirely and takes the compiler's own default. `Debug` and `RelWithDebInfo` ignore
the setting and are always generic.

The published container images are built at all three levels; see [docker.md](docker.md).

# Build steps (Update)

Tags follow semver (example: `v1.0.0`).

```bash
git fetch --tags
git checkout <TAG>
git submodule update --init --recursive
cmake -DCMAKE_BUILD_TYPE=System -B build
cmake --build build -j$(nproc) --target IDHANServer
```

# [Getting started](setup.md)

Now you can get started setting up IDHAN
