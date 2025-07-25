# Build steps (First time)

Note: Replace BUILD_TYPE with either `Release` or `Debug` depending on your requirements, If you are running this on the system you are building on, use `System` instead

```
git clone https://github.com/KJNeko/FGLEngine.git --recursive
cmake -DCMAKE_BUILD_TYPE=System -B build
cmake --build build -j<THREAD COUNT> --target IDHANServer
```

# Build steps (Update)

Tags will be in a semver format (Example: `v1.0.0`)

```
git checkout <TAG>
git pull
git submodule update --init --recursive
cmake -DCMAKE_BUILD_TYPE=System -B build
cmake --build build -j<THREAD COUNT> --target IDHANServer
```
