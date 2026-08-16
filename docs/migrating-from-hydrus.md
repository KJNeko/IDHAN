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