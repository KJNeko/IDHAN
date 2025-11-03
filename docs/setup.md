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

A cluster is a file location that IDHAN can put files, It will expect all files placed there to be named with their hash
and have an extension, It will ONLY make changes or place files if it is NOT set to readonly, which is the default for
any new cluster during creation.

To easily make a cluster without any 3rd-party tool. You can start the server and access the swagger api docs via
`/docs` If this does not result in a valid webpage or has errors, See the troubleshooting guide

One you've opened the swagger docs, go down to `clusters` and find `/clusters/add`, Expand it and hit `try it out`
from there you can then change the template json to fit your requirements. If the creation succeeds you should see a
result that contains much of the information you've just entered, as wel as some extra info along with a cluster_id.
Once you've done that you can then hit the scan endpoint (Read below)

## Scanning a cluster

To scan a cluster you can use the `/clusters/{id}/scan` endpoint. However, please read the swagger docs for the various
parameters you can enter. The defaults *should* work for the most part, If the cluster was not created as readonly it
might make some changes to the structure without asking. If this is a worry for you and you have NOT set the cluster to
readonly. You can either modify it to be readonly, or scan with `force_readonly=true` as one of the query parameters.

## Tagging/Getting files

There are too many things to list here for a simple getting started guide, Please see the swagger docs for the various
tag endpoints. Note that files will NOT be returned in a search UNLESS they've been scanned in a cluster first. Even if
tagged.

## Getting hydrui/hyweb to work
If you are not self hosting either, you'll need to setup either a proxy to provide https, or add in your own self-signed keys to IDHAN for it to function properly.


### Self-signed keys

You can run `openssl req -x509 -newkey rsa:4096 -keyout server.key -out server.crt -days 365 -nodes` in a directory that you're happy with for the keys to be in, and then provide IDHAN with the information to use the keys
Set the following in one of your config files for IDHAN
```aiignore
[host]
use_ssl = true
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
