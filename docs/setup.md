# Getting started

## Config

The first thing you'll want to do is set up some configs

Note: Each config will be listed as `[group] name`, this can either be set via an ENV variable `IDHAN_GROUP_NAME` or in
one of the toml files

Example:

```toml
[group]
name = value
```

### Thumbnails

The main thing to set up here will be where IDHAN will place thumbnails it's generated via one of the thumbnail
generators. To do so you'll want to set a path for `[thumbnails] path`

### Postgres

The main values to set here will be
(all are in group `[database]`)

- `host` - The host or ip of the pg database
- `user` - The user to attempt to sign in with
- `password` - the password to try to use
- `database` - The database name

IDHAN will create a public schema if it does not exist, as well as all the other tables. Note that if you are starting
from scratch, Completely wipe the schema that was created last time, As IDHAN will also create some functions that won't
be wiped if you just drop all the tables.

## Creating your first cluster

###### If you are coming from Hydrus, This is just a file path set in the 'database' management stuff.

A cluster is a file location that IDHAN can read files from and write files to. IDHAN expects files in a cluster to be
named by their hash with an extension. A cluster marked **readonly** will never have files added, moved, or renamed by
IDHAN — it will only index what is already there.

**New clusters default to readonly.** This is intentional: it gives you a chance to verify the scan results before
allowing IDHAN to make any changes to your files. If you are pointing IDHAN at an existing collection (e.g. migrating
from Hydrus or another tool), keep readonly enabled until you are confident the scan looks correct.

To create a cluster, start the server and open WebUI (`ip:port/`), If this is your first run you will need to also
supply an API key, This can be gotten by accessing `ip:port/generate_api_key`, This can **ONLY** be used **ONCE**.

Start by adding the 'Cluster Manager' panel from the 'Add panel...' menu

![addPanelExample.webp](images/addPanelExample.webp)

Once you have added that you can then add in your path

![ClusterMenu.webp](images/ClusterMenu.webp)

### Expected file structure

IDHAN organises files inside a cluster using the file's SHA-256 hash. Files are placed in a two-character prefix
subdirectory derived from the first two hex characters of the hash:

```
cluster_root/
  f{hex[0:2]}/
    {full_sha256}.{ext}
  bad/                   # created by a non-readonly scan; files that fail validation are moved here
```

For example, a JPEG whose hash starts with `ab` would be at:

```
cluster_root/fab/ab3f7c....jpg
```

If you are pointing IDHAN at an existing collection the files **must already follow this layout**. Any file that does
not match the expected name or is in the wrong subdirectory will be treated as missing or corrupted during a scan.

> **Note:** Thumbnails are **not** stored in clusters. They are written to a separate path configured via
> `[thumbnails] path` in the config file (default: `./thumbnails`), using the layout
> `t{hex[0:2]}/{sha256}.thumbnail`.

## Scanning a cluster

To scan a cluster you can hit the 'Scan' menu which will show you the options for the scan. If you are coming from
another program, or want IDHAN to simply 'eat' the files, Check 'Adopt Orphans', This will make IDHAN register the files
and run them through the import process. Note that if you are doing this on an ALREADY EXISTING directory that IDHAN has
managed for awhile, It is recomended to keep this off, As IDHAN will register invalid files that shouldn't be in the
directory (such as files with corruption) without any form of warning or error.

## Where did my imported files go?

Currently there is no proper workflow system for imported files. That will be coming later. But for now you can view
EVERY file by simply doing an empty search with no tags. You can then sort by imported time.

## Swagger Docs

If you are interested in using the API endpoints directly. There is a swagger docs built and available at the `/api`
endpoint. If you don't know what this means then it's probably not for you.

## Getting hydrui/hyweb to work

> Note that you must give the path to the hydrus 3rd-party program as `<ip>:<port>/hyapi`.

If you are not self hosting either, you'll need to setup either a proxy to provide https, or add in your own self-signed
keys to IDHAN for it to function properly.

### Self-signed keys

You can run `openssl req -x509 -newkey rsa:4096 -keyout server.key -out server.crt -days 365 -nodes` in a directory that
you're happy with for the keys to be in, and then provide IDHAN with the information to use the keys
Set the following in one of your config files for IDHAN

```toml
[host]
use_tls = true
server_cert_path = "/home/whatever/yourpath/server.crt"
server_key_path = "/home/whatever/yourpath/server.key"

# if either are set to "" then it will just not listen on it

ipv4_listen = "127.0.0.1" # localhost only
# or
ipv4_listen = "0.0.0.0" # all

ipv6_listen = "::1" #localhost
# or
ipv6_listen = "::" # any
```
