# IDHAN design notes

This document holds the key ideas and concepts behind IDHAN's design, current and future. It is a
guide for staying on track, not a description of what the code does today.

> **This is a roadmap/vision document, not a reference.** Sections here describe intended design and
> may be ahead of (or diverge from) the implementation. For example, the "Siblings" and "Resolution
> order" sections describe planned behaviour, whereas `tag_siblings` is currently read-only, with no
> endpoint that writes to it. For the *authoritative* current state see
> [Database Schema](database-schema.md) and the [Tag System Trigger Review](tag-system-triggers.md).
>
> Implementation details (types, module loading, endpoint shapes) are deliberately kept out of this
> document. They belong in the reference docs, where they can be checked against the code.

The application/concept name is **IDHAN** (**I** **D**on't **H**ave **A** **N**ame).

## Conventions

Anything marked **(P)** is planned, but very likely will not be implemented for the first version of
IDHAN.

## Key terminology

| Name         | Meaning                                                                            |
|--------------|------------------------------------------------------------------------------------|
| Record       | The core entity. One record is one piece of media, identified by its SHA-256 hash. |
| Tag          | A text string of two components, a namespace and subtag.                           |
| Namespace    | 1st component of a tag, used to group subtags together.                            |
| Subtag       | 2nd component of a tag.                                                            |
| Tag domain   | A grouping of tag *mappings*, so tags from different sources can be kept apart.    |
| File cluster | A storage point for files.                                                         |
| Generator    | A module that derives new files from a stored one, rather than storing each.       |
| Collection   | **(P)** A grouping of records that can carry tags of its own.                      |

# Records

A record is the core information structure for IDHAN. It is identified by the SHA-256 hash of the
file it represents, and referenced internally by a numeric ID.

A record is deliberately thin: it is an identity and nothing more. Everything else hangs off it, such
as where the bytes live, what the file is, and what it has been tagged. That separation lets a record
exist before its file does, and survive the file being removed from a cluster.

A record can be linked to many different things depending on what it needs to represent.

### Many to many

- **Tags**. Records can have tags associated with them.
- **Collection** **(P)**. Records can be placed into a collection for organization.

### One to one

- **File info**.

# File info

File info is what IDHAN knows about a record *as a file*: how big it is, what type it is, which
cluster holds it, and when it arrived.

The important idea is that file info is separable from the record. A record whose file has been
removed from a cluster keeps its identity, its tags, and its history. Only the location becomes
empty. This is what makes a record usable as a permanent reference: tags applied to it stay
meaningful whether or not the bytes are currently on disk.

Type detection is a two-layer idea. A record has a MIME type, which is the coarse answer, and
alongside it whatever richer metadata the format carries: dimensions and channel count for an image,
duration for a video, layer structure for a project file. The coarse layer is what search and storage
decisions run on; the rich layer is whatever the parser for that format thought was worth keeping,
and is deliberately open-ended rather than a fixed set of columns.

# Tags

Tags are made up of two components, a namespace and a subtag. Namespaces are used as a way to group
various tags together simply. The subtag is the main visible component of a tag, used to represent
information about the media being tagged.

Both components are interned separately, and the pair itself is what gets referenced as a tag. The
consequence worth caring about is that the same subtag under two namespaces is two distinct tags with
no relationship to each other.

Tags are always displayed and stored as lowercase.

Examples of common namespaces:

- `character`
- `series`
- `creator`

Examples of a subtag:

- `skirt`
- `blue eyes`

Tag components are separated by a `:` character. In the event that a namespace is 'empty' or blank,
then there is no separation character, and only the subtag should be displayed.

Examples of completed tags:

- `character:toujou koneko`
- `series:highschool dxd`
- `catgirl` (notice the lack of a namespace)

For inputs, the first separation character (`:`) is used. An example of this is in the case of
`series:re:zero` the namespace is `series` and the subtag is `re:zero`.

## Domains

Tags are also assigned a 'domain'. Domains group tag mappings to allow easy data manipulation at
scale. An example of this would be creating a domain for tags that are identified by an AI model.
This would prevent the model from possibly messing up tags that might be from a remote tag set, which
would also be given its own domain.

The unit being grouped is the *mapping*, not the tag. The same tag can be applied to the same record
in several domains at once, and a search chooses which domains it reads from.

## Aliases

In some cases there might be sites that use slightly different meanings for the same thing. An
example of this would be comparing two boorus where one might use `dress:blue` and `blue dress`. An
alias allows a non-destructive renaming of a tag from one to another. The naming internally for this
is `aliased_id -> alias_id`. Any attempt of aliasing an already aliased id should result in an error.

If you are familiar with Hydrus, this is what Hydrus calls 'siblings'. IDHAN uses that word for a
different relationship, described below.

## Parents / children

In some cases a tag might always be associated with another tag. The tags would almost always be
together. In this case a parent/child relationship can be made. This relationship dictates that a
child cannot be without its parents. A tag can have any number of parents. An example of this would
be the tag `pussy` with the parent tag `rating:explicit`.

## Siblings

Siblings are different from Hydrus. In IDHAN they work as an exclusive tagging. The best example of
this is rating tags. The tag `rating:safe` should obviously never be with the tag `rating:explicit`,
as something can't be both. This prevents that from happening.

In this relationship two tags are designated as 'siblings', one being the 'older sibling', the other
being the 'younger sibling'. If both siblings are present, then the older one is presented while the
younger one is hidden.

## Resolution order

Tags are 'solved' in the following order:

- Parents/children and siblings are flattened. This means that all tags are transformed into their
  alias tag, or 'idealized'.
- Parent/child tags are then applied.
- Finally, siblings are applied.
- When siblings are applied, a parent that is younger than an older tag is hidden. If the child is
  supposed to be hidden, then all parents are also hidden or removed, even if both tags were present
  on the record initially.

Things to note:

- If child A and parent B are on a record with the parent being added due to child A existing, and
  parent B is the younger tag of an exclusive-or tag, then the child should remain with the parent
  removed. If the child is the younger tag and should be hidden, then the parent should not be
  displayed.

# File clusters

A file cluster is the storage point for files. Clusters are managed via the REST API (see the `/api`
Swagger docs or [Getting started](setup.md)).

The design intent is that a collection can be spread over several clusters without the user tracking
which file went where. A cluster therefore carries enough policy for the server to make that choice
on its own:

- The path to store the files at.
- Whether the cluster may be written to at all, or only indexed. Pointing IDHAN at an existing
  collection must never require it to modify that collection.
- What kinds of file may be stored: the files themselves, thumbnails, or both.
- The share of new files this cluster should receive, expressed as a ratio against the other
  clusters rather than an absolute number.
- The maximum byte size the cluster should reach.

# File parsing

IDHAN is made to be expandable. Understanding a new format should never mean changing the server.

The extension points are the ones a format actually needs: identifying what a file is, pulling
metadata out of it, producing a thumbnail for it, and deriving other files from it. A backend
declares which MIME types it handles and the server routes work to it accordingly.

The other half of that intent is containment. A parser is the part of a media server most likely to
be handed hostile input, so a misbehaving one must not be able to take the server down with it. See
[known issues](known-issues.md) for how far that has actually been taken.

For how modules are built and hosted today, see `IDHANModules/include/` and the module section of
`CLAUDE.md`.

# Generators

Generators provide a way for IDHAN to keep a record of a source of multiple files. An example would
be storing a single PSD and generating image variants without needing to store each variant
separately.

The idea is that a derived file does not have to be a stored file. If IDHAN knows the source and the
transformation, it can produce the variant on request and decide separately whether keeping it is
worth the disk. A generated file is therefore a record like any other, with the storage decision
independent of its existence.

# Collections **(P)**

In IDHAN a collection is simply a way to tie multiple records together. A collection can be made to
inherit all the tags of its members, and even have tags of its own. The tags of a collection follow
the same rules as those of individual records.

Not designed further than this yet, and not implemented; there is no collections table. The open
questions are how a collection's inherited tags interact with alias and parent resolution, and
whether a record may belong to more than one collection.

# Scraping **(P)**

Not designed yet. For the time being IDHAN will only allow 'scraping' via the API. That is, an
external tool fetches, and IDHAN receives the results through the same endpoints any other client
would use, rather than IDHAN reaching out to sites itself.
