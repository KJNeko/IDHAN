This document contains a lot of the key ideas and concepts for the current and future development.

The application/concept name is **IDHAN** (**I** **D**on't **H**ave **A** **N**ame).

# Key terminology

## Record

A record is the core information structure for IDHAN. In the core library this is designated by a ID of type `std::uint64_t`.
Various things can be associated with a record. Some in a many to many, some in a one to one.

### Many to Many

- Tags

### One to One

- FileInfo

#### Tags

Tags are made up of two components, A namespace, And a subtag. Namespaces are used as a way to group various tags together simply.
The subtag is the main visible component of a tag, It is used to represent information of the media being tagged.

These two components are represented as `namespace_id` and `subtag_id` both of which use the type `std::uint32_t`.
They are combined into a tag that is given the id `tag_id` of the type `std::uint64_t`

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

In some cases there might be sites that use slightly different meanings for the same thing. An example of this would be comparing two boorus where one might use `dress:blue` and `blue dress`. An alias allows a non-destructive renaming of a tag from one to another. If you are familiar with Hydrus then the name for this system there is called 'siblings'. Siblings in IDHAN function differently.

### Parents/Child

In some cases a tag might always be associated with another tag. The tags would almost always be together. In this case a parent/child relationship can be made. This relationship dictates that a child cannot be without it's parents. A tag can have any number of parents. An example of this would be: the tag `pussy` with the parent tag `rating:explicit`.

### Siblings

Siblings are different from Hydrus, In IDHAN they work as an exclusive tagging. The best example of this is rating tags. The tag `rating:safe` should obviously never be with the tag `rating:explicit`. As something can't be both. This prevents that from happening. In this relationship two tags are designated as 'siblings', One being the 'older sibling', the other being the 'younger sibling'. If both siblings are present, Then the older one is presented while the younger one is hidden.




