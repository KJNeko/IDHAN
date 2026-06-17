# Build steps (First time)

Replace `BUILD_TYPE` with `Release` or `Debug` depending on your requirements. Use `System` when building on the machine you will run the server on.

```bash
git clone https://git.futuregadgetlabs.net/kj16609/IDHAN.git --recursive
cmake -DCMAKE_BUILD_TYPE=System -B build
cmake --build build -j$(nproc) --target IDHANServer
```

To also build the HydrusImporter:

```bash
cmake --build build -j$(nproc) --target IDHANServer HydrusImporter
```

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