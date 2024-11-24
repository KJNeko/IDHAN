This document contains a lot of the key ideas and concepts for the current and future development.

The application/concept name is **IDHAN** (**I** **D**on't **H**ave **A** **N**ame).

# Notes

- Anything mark with **(P)** means that the idea is planned however will very likely not be implemented for the first version of IDHAN.

# Key terminology

| Name         | Meaning                                                  |
|--------------|----------------------------------------------------------|
| Record       |                                                          |
| Tag          | A text string of two components, a namespace and subtag. |
| Namespace    | 1st component of a tag, Used to group subtags together   |
| Subtag       | 2nd component of a tag                                   |
| File Cluster | A storage point for files.                               |

# Records

A record is the core information structure for IDHAN. It can be referenced as a 64 bit ID.
A record can be linked to many different things depending on what it needs to represent.

### Many to Many

- Tags
- Collection

### One to One

- FileInfo

# Tags

Tags are made up of two components, A namespace, And a subtag. Namespaces are used as a way to group various tags together simply.
The subtag is the main visible component of a tag, It is used to represent information of the media being tagged.

These two components are represented as `namespace_id` and `subtag_id` both of which use the type `std::uint32_t`.
They are combined into a tag that is given the id `tag_id` of the type `std::uint64_t`

Tags are also assigned a 'domain', Domains are used to group tags to allow easy data manipulation at scale, An example of this would be creating a domain that is for tags that are identified by an AI model. This would prevent the model from possibly messing up tags that might be from a remote tag set, Which would also be given its own domain.

Tags are always displayed and stored as lowercase.

Examples of common namespaces:

- `character`
- `series`
- `creator`

Examples of a subtag:

- `skirt`
- `blue eyes`

Tags components are seperated by a `:` character. In the event that a namespace is 'empty' or blank, then there is no seperation character. And only the subtag should be displayed.

Examples of completed tags:

- `character:toujou koneko`
- `series:highschool dxd`
- `catgirl` (Notice the lack of a namespace)

For inputs, The first separation character (`:`) is used. An example of this is in the case of `series:re:zero` the namespace is `series` and the subtag is `re:zero`

### Aliases

In some cases there might be sites that use slightly different meanings for the same thing.
An example of this would be comparing two boorus where one might use `dress:blue` and `blue dress`.\
An alias allows a non-destructive renaming of a tag from one to another.\
If you are familiar with Hydrus then the name for this system there is called 'siblings'.
Siblings in IDHAN function differently. The naming internally for this is `aliased_id -> alias_id`.\
Any attempt of aliasing an already aliased id should result in an error.

### Parents/Child

In some cases a tag might always be associated with another tag. The tags would almost always be together. In this case a parent/child relationship can be made. This relationship dictates that a child cannot be without it's parents. A tag can have any number of parents. An example of this would be: the tag `pussy` with the parent tag `rating:explicit`.

### Siblings

Siblings are different from Hydrus, In IDHAN they work as an exclusive tagging. The best example of this is rating tags. The tag `rating:safe` should obviously never be with the tag `rating:explicit`. As something can't be both. This prevents that from happening. In this relationship two tags are designated as 'siblings', One being the 'older sibling', the other being the 'younger sibling'. If both siblings are present, Then the older one is presented while the younger one is hidden.

### Order of application (Probably a shitty name for this)

Tags are 'solved' in the following order

- parents/childs and siblings are flattened. This means that all tags are transformed into their alias tag, or 'idealised'.
- parent/child tags are then applied.
- Finally siblings are applied.
- When siblings are applied a parent that is younger then an older tag is hidden, If the child is supposed to be hidden, then all parents are also hidden or removed, Even if both tags were present on the record initally.

Things to note:

- If child A and parent B are on a record with the parent being added due to child A existing, and parent B is the younger tag of an exclusive or tag, then child should remain with the parent removed. If the child is the younger tag and should be hidden, then the parent should not be displayed.

# File info

A record doesn't have to exist with a file info, But it's very likely to have one.
Internally there are a few generic enum values to represent what the file is. This is mainly used for hints on how to handle a file.

Enum values:

| Name       | Value    | Description                                                                                                                                                                                    |
|------------|----------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Media File | `0b0001` | A Media file is the most common type, This can be a file such as a image or video. Examples: PNG, JPG, MPV, MP4, Ect                                                                           |
| Generator  | `0b0010` | This is a file that contains information to generate other files. An example of this is a PSD or Krita art file.                                                                               |
| Generated  | `0b0100` | This marker indicates that the file has been generated by a generator and can be replicated again. The config for generating this file should be stored and which record it was generated from |
| Virtual    | `0b1000` | This is the same as Generated however the file itself is not stored in any file cluster, It should be generated again when needed                                                              |

This information set also contains basic information about the file:

- media size (byte size of the media, even virtual files will contain this after being generated)
- store time. The time in which a file was placed into a cluster, if the file is virtual this is the same as the generator store time.

# File parsing

Even if IDHAN can identify a file, it will ignore any MIME types not registered to be handled by IDHAN. There are defaults registered by default such as JPG and PNG and other common formats. A full list can be found here:(TODO: Put the list here)

## Importing methods

IDHAN will parse files that it has been given to import through the following methods:

### Internal parser

IDHAN will use an internal MIME parser that works based on byte signatures. Each MIME will be given a set of signatures and their expected locations, and a priority, The priority is to allow differentiation between APNG and PNG.

### Python scripts **(P)**

IDHAN will allow for python scripts to be used in order to identify the mime type of file, This will happen after the internal parser has run and found no results, Or if IDHAN has been specified to use the script after a specific parser hit.\
IDHAN will supply a series of helper functions in python that can be imported in python to assist with giving back the response IDHAN expects.\
These scripts will also be capable of returning file information back to IDHAN that can help with understanding new filetypes that IDHAN has never seen before without adding handling in the source code itself.

# Scraping **(P)**

**(P)**: IDHAN will only allow 'scraping' via the API for the time being

**FOR NOW THIS IS A STUB**

