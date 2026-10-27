> This is assuming you've already gotten IDHAN setup.

# Getting files into IDHAN

Start by making a Cluster, You should set the cluster path to the existing `hydrus_client` folder. Ensure that the
structure is like `hydrus_client/faa/aa...jpg` and so on. Set this cluster to 'Read-Only' while you are doing your
migration, You can set it writable later.

Once you have gotten the cluster added you can then scan it. Ensure that 'Adopt Orphans' is enabled, Otherwise IDHAN
will not register any files as being imported. Note that the 'Adopt-Orphans' flag will still force a mime scan, metadata
scan, and will verify the hash matches the filename, Later scans will trust the hash in the filename.

# Importing tags

You can import tags using the HydrusImporter, This also has a PTR Importer if you choose to use it.

Download `HydrusImporter-<version>-linux-x86_64.tar.gz` and its `.sha256` file from the
[GitHub release](https://github.com/KJNeko/IDHAN/releases). The Linux x86_64 build includes
Qt, its platform plugins, and the importer libraries. It is built on Ubuntu 26.04 and requires
a compatible Linux system; older distributions may need a source build.

From the directory containing both downloaded files, verify and extract the archive, then start the importer:

```sh
sha256sum -c HydrusImporter-<version>-linux-x86_64.tar.gz.sha256
tar -xzf HydrusImporter-<version>-linux-x86_64.tar.gz
./HydrusImporter/HydrusImporter
```

Replace `<version>` with the release tag, including its `v` prefix. Keep the extracted directory
together so the launcher can find its bundled libraries and Qt plugins.

For Windows x86_64, download `HydrusImporter-<version>-windows-x86_64.zip` from the same
release, extract it, and run `HydrusImporter.exe` in the `HydrusImporter` directory. The ZIP
includes Qt, the compiler runtime, and the required DLLs; keep them beside the executable.
The accompanying `.sha256` file contains the SHA-256 checksum, which you can compare with
PowerShell's `Get-FileHash -Algorithm SHA256` for the downloaded ZIP.
