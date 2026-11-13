The basic idea here is that the site/url class defines the user-facing aspects of sites. The possible URLs and ways to
search it.

A site/url class contains:

- ID
- version
- list of known hosts
    - if no scheme assume https
    - IDHAN can add to this list at runtime.
- url patterns
    - pattern (see below)
    - human name
    - optionally the ID of a generator which has params that are captured by the url pattern
- generators (name can be changed)
    - ID (parsers declare functions to handle these)
    - human name
    - params (not sure exactly how this should look but it should describe the parameters the generator accepts
      including whether its optional, possible enum options, probably fine to have param values all be strings)

parsers declare functions to handle generators. These functions always receive the domain and any generator params (can
have no params). If a url pattern match directed to a generator the url is included in what the parser gets. This allows
cases where you just want to deal with the url entirely in the parser and thus have an empty generator.

URL patterns are matched in order so one could add a catachall at the end to send all otherwise unmatched urls to a
generator.

I also generally think the url class should have a top level ID and the IDs of the items within it dont need the
namespace included. I think it would be fine to have each parser declare it supports a list of url classes and be
expected to handle all the generators within that url class. If there are things that we want to reuse between parsers
of course that's where just using JS and being able to import shit from other files is nice.

So also the neat thing about this generator concept is it allows really nice rich UIs for setting up searches since
clients can discover the possible parameters for each generator.

I'm still thinking about whether the url class should declare if any user provided headers or auth info is needed vs the
parser declaring that, probably the parser since it can be more of a specific detail to the APIs the parser uses.

## URL Patterns

I have been looking at and basing my examples on the web standard URL Pattern
API: https://developer.mozilla.org/en-US/docs/Web/API/URL_Pattern_API

This works well for paths but when it comes to query params it falls short because it can't handle arbitrary query param
ordering. So pending finding a better alternative I am using my own slightly modified concept for url patterns where
query params are split up by matching their keys and values matched separately called "query" rather than the "search"
pattern in the actual spec. See the gelbooru example below.

## Examples:

(using the above mentioned web URL Pattern API)

```json
{
  "id": "hyshare",
  "version": 1,
  "hosts": [
    "example.invalid"
  ],
  "url_patterns": [
    {
      "patterns": [
        {
          "pathname": "/gallery/:id"
        }
      ],
      "name": "hyshare gallery",
      "generator": "gallery"
    },
    {
      "patterns": [
        {
          "pathname": "/view/:hash"
        }
      ],
      "name": "hyshare file page",
      "generator": "view"
    },
    {
      "patterns": [
        {
          "pathname": "/file/:hash"
        }
      ],
      "name": "hyshare file"
    }
  ],
  "generators": [
    {
      "id": "gallery",
      "name": "hyshare gallery",
      "params": {
        "id": {
          "name": "gallery id",
          "type": "string",
          "pattern": "/^[a-zA-Z0-9][a-zA-Z0-9-_ ]*$/"
        }
      }
    },
    {
      "id": "view",
      "name": "hyshare file",
      "params": {
        "hash": {
          "name": "file hash",
          "type": "sha256"
          //maybe have some predefined types instead of having to repeat common regex patterns
        }
      }
    }
  ]
}
```

```json
{
  "id": "mastodon",
  "version": 1,
  "hosts": [
    "baraag.net",
    "pawoo.net",
    "mastodon.social"
  ],
  "url_patterns": [
    {
      "pattern": {
        "pathname": "/home"
      },
      "name": "mastodon home page",
      "generator": "timeline"
    },
    {
      "pattern": {
        "pathname": "/explore"
      },
      "name": "mastodon explore page",
      "generator": "trending"
    },
    {
      "pattern": {
        "pathname": "/favourites"
      },
      "name": "mastodon favourites page",
      "generator": "favourites"
    },
    {
      "pattern": {
        "pathname": "/bookmarks"
      },
      "name": "mastodon bookmarks page",
      "generator": "bookmarks"
    },
    {
      "patterns": [
        {
          "pathname": "/@:username{/(media|with_replies|tagged)}?"
        }
      ],
      "name": "mastodon user",
      "generator": "user"
    },
    {
      "patterns": [
        {
          "pathname": "/@:username/featured"
        }
      ],
      "name": "mastodon user featured posts",
      "generator": "user_featured"
    },
    {
      "pattern": {
        "pathname": "/@:username/:id(\\\\d+)"
      },
      "name": "mastodon post",
      "generator": "post"
    }
  ],
  "generators": [
    {
      "id": "home",
      "name": "mastodon home timeline"
    },
    {
      "id": "trending",
      "name": "mastodon trending timeline"
    },
    {
      "id": "favourites",
      "name": "mastodon favourites timeline"
    },
    {
      "id": "bookmarks",
      "name": "mastodon bookmarks"
    },
    {
      "id": "user",
      "name": "mastodon user timeline",
      "params": {
        "username": {
          "name": "username",
          "type": "string"
        }
      }
    },
    {
      "id": "post",
      "name": "mastodon post",
      "params": {
        "username": {
          "name": "username",
          "type": "string",
          "optional": true
        },
        "id": {
          "name": "post id",
          "type": "string",
          "pattern": "\\d+"
        }
      }
    }
  ]
}
```

```json
{
  "id": "gelbooru",
  "version": 1,
  "hosts": [
    "gelbooru.com"
  ],
  "url_patterns": [
    {
      "patterns": [
        {
          "pathname": "/index.php",
          "query": {
            "page": "post",
            "s": "list",
            "tags": ":tags"
          }
        }
      ],
      "name": "gelbooru tag search",
      "generator": "search"
    },
    {
      "patterns": [
        {
          "pathname": "/index.php",
          "query": {
            "page": "post",
            "s": "view",
            "id": ":id"
          }
        }
      ],
      "name": "gelbooru post page",
      "generator": "post"
    },
    {
      "patterns": [
        {
          "pathname": "/file/:hash"
        }
      ],
      "name": "hyshare file"
    }
  ],
  "generators": [
    {
      "id": "search",
      "name": "gelbooru tag search",
      "params": {
        "tags": {
          "name": "tags",
          "type": "string"
        }
      }
    },
    {
      "id": "post",
      "name": "gelbooru post",
      "params": {
        "id": {
          "name": "post id",
          "type": "string",
          "pattern": "\\d+"
        }
      }
    }
  ]
}
```
