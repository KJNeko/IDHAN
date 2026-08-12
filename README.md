# Welcome to IDHAN's page

IDHAN is a media management and archival program written in C++ for people with large collections of media.
It's designed in a way that is compatible with most booru models. Currently, the server is only built for running on
Linux.

![Screenshot_20260812_082553.png](../../../IDHANImages/Screenshot_20260812_082553.png)

###### Yes this is a filter, If the 4 letter word scares you then go away.

# What IDHAN is capable of

- [x] Tagging images
- [x] Searching via tags
- [x] Metadata searching
- [x] Importing new files
- [x] Plugins for handling more files
- [x] Plugins for WebUI panels
- [x] Functioning WebUI

What is unfinished

- [ ] Downloading files

# Hydrus vs IDHAN

IDHAN is based on Hydrus from a use perspective, But it has some different core features that might be of use compared
to what Hydrus offers.

If you want:

- Simple setup
- Usage on a single machine

You likely want to just use [Hydrus](https://github.com/hydrusnetwork/hydrus) instead.

You are expected to have some knowledge to configure and tinker on your own without direct assistance.

# Terminology

## Tags

Tags are split into two parts. A namespace component and a subtag component. The namespace component is used to
categorize tags. For example, `artist:`, `character:`, `species:` are all namespaces. The subtag component is the actual
tag. For example, `character:toujou koneko` is a namespace `character` and subtag `toujou koneko`, Note that
`character:toujou koneko` is not the same tag as `toujou koneko`

## Cluster

A cluster is a folder that files will be placed into, or read from. Files are organized using a two-level directory
structure based on their SHA256 hash: `f{first 2 hex chars}/{full hash}.ext` (for example, `fab/ab123...def.png`).

# Getting started

## Docker

Master: ![Master Build](https://git.futuregadgetlabs.net/kj16609/IDHAN/actions/workflows/docker-build.yml/badge.svg?branch=master)

Dev ![Dev Build](https://git.futuregadgetlabs.net/kj16609/IDHAN/actions/workflows/docker-build.yml/badge.svg?branch=dev)

For detailed docker instructions, please refer to [Docker Guide](docs/docker.md)

## How to build

For detailed build instructions, please refer to our [Build Guide](docs/build.md).

## How to configure

For configuration options and setup, check out our [Configuration Guide](docs/config.md).

# Server docs

The server listens on port **16609** by default. Interactive API docs (Swagger UI) are available at `/api` when the
server is running.

A hosted version of the API docs is available at [idhan.futuregadgetlabs.net](https://idhan.futuregadgetlabs.net). (Once
I get this working that is)