//
// Created by kj16609 on 12/18/24.
//
#pragma once
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>

#include "filesystem/io/IOUring.hpp"
#include "MimeIdentifier.hpp"
#include "MimeInfo.hpp"
#include "ModuleBase.hpp"

namespace idhan::mime
{
class Cursor;

/**


@page MimeParser Mime Parser

@subsection MimeParserFile idhanmime file
the following section identifies how the mime parser json file should be laid out.
`//` comments are permitted.

the current fields are:
- `mime` is a string that identifies the mime, an example of this is `image/jpeg` for a jpg image.
- `extensions` is an array of extensions the file could possibly possess, this is not used in the detection but is for
the user to select one of the extensions to store the files as, the default is the 0th item. If this list is empty the
extension `bin` is used instead
- `data` is an array of json objects. The format for this objects is listed in the next section
- `priority` a non-negative integer used to rank multiple positive matches, highest wins (default 25). Ties are broken
by mime name.
- `require_extension` a boolean; if true the identifier only matches when the file's extension is in `extensions`
(default false). Note that scans without a file name (e.g. raw octet-stream uploads) can never match such identifiers.

@subsubsection MimeParserFileData data
- `type`: the only required field is a `type` field that signifies the type of operation taking place.
- `data`: any matcher may carry a nested `data` array of child matchers. Children run only if the matcher itself
passed, and continue from the position where its match ended. Sibling matchers each start from their parent's
position.

List of types:
- search
- include

@subsubsection MimeSearch search
optional fields:
- `offset`: an integer. If present, the match is tested at exactly this position. Negative values signify an offset
from the end of the data (-1 indicates the last byte). If absent, the data is scanned forward from the current
position until a match is found.
- `limit`: a positive integer bounding how many scan positions are tried (only meaningful without `offset`).
Unlimited if absent.

required fields:
- `hex`: a hex string of the data to match, or an array of hex strings of which any one matching passes (used e.g.
for the mp4 brand list). Patterns must be non-empty and of even length.

@subsubsection MimeInclude include
required fields:
- `file`: the file name of another idhanmime file whose `data` matchers are run as part of this matcher. Circular
includes are rejected at load time.

An example file
```json
{

  "mime": "image/png",
  "extensions": [
	"png"
  ],
  "data": [
	{
	  "type": "search",
	  "offset": 0,
	  "hex": "89504E470D0A1A0A"
	}
  ]
}
```
 */

//! This mime type is used for unknown mime types
constexpr auto INVALID_MIME_NAME { "unknown/unknown" };

class MimeDatabase
{
	MimeDatabase();

	friend std::shared_ptr< MimeDatabase > getMimeDatabase();

	// Copy-on-write: scans hold a snapshot of this pointer across their co_awaits, so a
	// concurrent reloadMimeParsers() swapping in a new vector can never invalidate what
	// an in-flight scan is iterating. The lock is only ever held for the pointer copy/swap.
	std::shared_ptr< const std::vector< MimeIdentifier > > m_identifiers {
		std::make_shared< std::vector< MimeIdentifier > >()
	};
	mutable std::mutex m_identifiers_mutex {};

	[[nodiscard]] std::shared_ptr< const std::vector< MimeIdentifier > > identifiers() const;

	drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > scan( Cursor cursor );

  public:

	[[nodiscard]] Json::Value dump() const;

	[[nodiscard]] drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > scan(
		std::string_view data,
		std::string file_name );

	[[nodiscard]] drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > scan( data_view data, std::string file_name );

	[[nodiscard]] drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > scan( FileIOUring file_io );

	[[nodiscard]] drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > scanFile( const std::filesystem::path& path );

	//! Reloads all the 3rd party mime parsers
	[[nodiscard]] drogon::Task< std::expected< void, drogon::HttpResponsePtr > > reloadMimeParsers();
};

[[nodiscard]] std::shared_ptr< MimeDatabase > getMimeDatabase();
[[nodiscard]] drogon::Task< std::expected< MimeID, drogon::HttpResponsePtr > > getMimeIDFromStr( std::string str, DbClientPtr db );

} // namespace idhan::mime
