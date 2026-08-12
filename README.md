# IDHAN

IDHAN is a media management and archival program written in C++ for people with large collections of media. It's
designed in a way that is compatible with most booru models. Currently, the server is only built for running on Linux.

IDHAN functions as a server that clients on other computers can connect to in order to organize and retrieve archived
and collected files.
It functions using a REST API hosted by the server. A React WebUI is also served by the server (seen in images below)
that allows you to interface with the server without a 3rd-party program. There is also a 'fake' Hydrus API that other
hydrus 3rd-party programs can use, Such as HyWeb and Hydrui.

IDHAN can be given plugins and modules to expand it's knowledge of files, It can be given new formats to
understand via mime parsing files, and eventually will be able to use more advanced parsing methods using
python scripts.

The Database Stats screenshot further down is a live instance holding 35.2 million
records, 12.7 million tags, and roughly 645 million tag mappings in a 79 GB database, indexing 589,000 files across
2.1 TB of disk.

Master: ![Master Build](https://git.futuregadgetlabs.net/kj16609/IDHAN/actions/workflows/docker-build.yml/badge.svg?branch=master)
Dev: ![Dev Build](https://git.futuregadgetlabs.net/kj16609/IDHAN/actions/workflows/docker-build.yml/badge.svg?branch=dev)

![The IDHAN WebUI running a tag search](docs/images/search.webp)

###### Yes this is a filter, If the 4 letter word scares you then go away.

The images below are screenshots of the WebUI. Some thumbnails in them have been blurred for this README; IDHAN does
not blur anything itself.

# Features

## Tag search

Tags are written as `namespace:subtag`. A query is a list of tags, `-negations`, and `system:` predicates (size,
dimensions, duration, and so on), with autocomplete on everything but the system predicates. Results can be sorted by
import time, creation time, file size, modified time, filetype, hash, or at random.

The **Breakdown** button reports, per term, how many records it matched, how many it removed, what was left afterwards,
and how long it took. This identifies which term is responsible when a search is slow.

![Tag search with the per-term breakdown table](docs/images/search.webp)

## Semantic search

IDHAN can embed your files with an ONNX model and search the collection by meaning rather than by tag. A query is a set
of weighted terms: plain text, a negative term to push results away, or `record:1234` to say "more like this file".
Vectors live in PostgreSQL via pgvector, indexed with HNSW over cosine distance. Models are yours to supply:
`tools/embedding-export/export_siglip2.py` exports siglip2 into the format the embedding module wants.

![Embedding search with weighted positive and negative terms](docs/images/embedding.webp)

## Tags, domains, and URLs

Tags are edited per **tag domain**, so tags you scraped from a downloader stay separate from the ones you applied
yourself, and you choose which domains a search looks at. Aliases, siblings, and parents are resolved by the database
itself. Inherited tags show up in the editor marked as such, rather than being silently copied onto the record.

Records also keep their known URLs, recording where a file came from.

![Tag editor and URL list for a single record](docs/images/urls_tags.webp)

## Notes

Free-text notes attached to a record, for information that does not fit the tag model.

![The notes panel](docs/images/notes.webp)

## Importing

Drag files in, or point the server at them. Each file is hashed with SHA256 on the way in, deduplicated against what
you already have, and handed to a metadata module that pulls out dimensions, channels, duration, and whatever else the
format carries. Imports report per-file results as they land.

![Importing files, with per-file results](docs/images/imports.webp)

## Storage clusters and database stats

Files live in **clusters**: directories the server writes into, or read-only ones it only indexes. A read-only cluster
lets IDHAN index an existing Hydrus store in place, without moving or copying the files.

The stats panel reports record and tag counts, on-disk versus database size, heap and index usage broken down per
table, and the collection split by mime type.

![Database statistics and cluster manager](docs/images/db_stats.webp)

## Configurable WebUI

Every part of the interface is a panel: search, grid, media viewer, record info, tags, notes, URLs, imports, jobs,
logs, cluster manager. Panels can be rearranged, and an arrangement can be saved as a layout.

Third-party panels load the same way the built-in ones do. See the [WebUI plugin guide](docs/webui-plugins.md).

## File format support

Thumbnailing, metadata parsing, and format conversion happen in **modules**. libvips, FFmpeg, PSD, and libarchive
backends are included.

Modules never run inside the server process; each is hosted by its own worker process, so a module that leaks memory,
corrupts its heap, or crashes takes down only that worker. A file is handed to a worker as a read-only descriptor it
maps, rather than as a buffer, so nothing is copied. A thumbnailer decoding one frame of a 4 GiB video faults in the
pages it touches and no more.

A worker is isolated, not sandboxed: it still runs with the server's filesystem access, and confinement is not yet
implemented. See [known issues](docs/known-issues.md) before treating module isolation as a security boundary.

Support for a new format is added by writing a module, without changing the server.

# Not done yet

- [ ] Downloading files

# Hydrus vs IDHAN

IDHAN is based on Hydrus from a use perspective, but it has some different core features that might be of use compared
to what Hydrus offers.

If you want a simple setup, or you only ever use one machine, you likely want to just use
[Hydrus](https://github.com/hydrusnetwork/hydrus) instead. IDHAN is a server: it expects multiple clients, a PostgreSQL
instance, and manual configuration.

You are expected to have some knowledge to configure and tinker on your own without direct assistance.

To migrate from Hydrus, `HydrusImporter` reads a Hydrus SQLite database and imports its records, tags, and URLs. Note
that Hydrus **siblings** are IDHAN **aliases**. IDHAN's "siblings" are a different relationship with no Hydrus
equivalent.

# Terminology

## Tags

Tags are split into two parts. A namespace component and a subtag component. The namespace component is used to
categorize tags. For example, `artist:`, `character:`, `species:` are all namespaces. The subtag component is the actual
tag. For example, `character:toujou koneko` is a namespace `character` and subtag `toujou koneko`, Note that
`character:toujou koneko` is not the same tag as `toujou koneko`

## Tag domain

A tag domain is a namespace for *mappings*, not for tags. The same tag can be applied to the same record in several
domains at once (manually, by a downloader, or by an import), and a search selects which domains it reads from.

## Cluster

A cluster is a folder that files will be placed into, or read from. Files are organized using a two-level directory
structure based on their SHA256 hash: `f{first 2 hex chars}/{full hash}.ext` (for example, `fab/ab123...def.png`).

A cluster can be marked read-only, in which case IDHAN indexes it but never writes to it.

# Getting started

## Docker

For detailed docker instructions, please refer to the [Docker Guide](docs/docker.md).

## How to build

For detailed build instructions, please refer to our [Build Guide](docs/build.md).

## How to configure

For configuration options, check out our [Configuration Guide](docs/config.md).

## First run

[Getting started](docs/setup.md) covers the config values you need, creating and scanning your first cluster, and TLS
setup.

# Server docs

The server listens on port **16609** by default. Interactive API docs (Swagger UI) are available at `/api` when the
server is running.

A hosted version of the API docs is available at [idhan.futuregadgetlabs.net](https://idhan.futuregadgetlabs.net). (Once
I get this working that is)

Additional documentation:

- [Database schema](docs/database-schema.md)
- [Tag system triggers](docs/tag-system-triggers.md)
- [Job system](docs/jobs.md)
- [WebUI plugins](docs/webui-plugins.md)
- [Known issues](docs/known-issues.md)
