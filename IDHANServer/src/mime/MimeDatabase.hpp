//
// Created by kj16609 on 12/18/24.
//
#pragma once
#include <condition_variable>
#include <expected>
#include <filesystem>
#include <memory>

#include "MimeInfo.hpp"
#include "fgl/defines.hpp"

namespace idhan::mime
{

/**


@page MimeParser Mime Parser

@subsection MimeParserFile idhanmime file
the following section identifies how the mime parser json file should be laid out.

the current fields are:
- `mime` is a string that identifies the mime, an example of this is `image/jpeg` for a jpg image.
- `extensions` is an array of extensions the file could possibly possess, this is not used in the detection but is for the user to select one of the extensions to store the files as, the default is the 0th item. If this list is empty the extension `.bin` is used instead
- `data` is an array of json objects. The format for this objects is listed in the next section
- `fast` a boolean value that informs the parser that if it passes it should consider the file found, default true

@subsubsection MimeParserFileData data
- `type`: the only required field is a `type` field that signifies the type of operation taking place.
- `id`: this is used for other components of the parser.

List of types:
- search
- override

@subsubsection MimeSearch search
optional fields:
- `offset`: either an integer value, or a string of an id for a compatible data search. Negative values signify a offset from the end of the data (-1 indicates 1 byte backwards from the size N)
- `strict`: signifies if the offset is a starting point, or the exact point. default true if not present

required fields:
- `hex`: a hex representation of the data. In order to signify wildcard data, this can also be an array of integer values, with any value beyond the 0-255 range being a wildcard, such as -1

@subsubsection MimeOverride override
Same requirements as @refitem MimeSearch but with an extra field
- `override`: The mime type to override to if this search passes.

An example file
```json
{

  "mime": "image/jpeg",
  "extensions": [
    "jpg"
  ],
  "data": [
    {
      "type": "search",
      "offset": 0,
      "hex": "89504E0D0A1A0A",
      "id": "signature"
    },
    {
      "type": "override",
      "strict": false,
      "offset": "signature:end",
      "hex": "6163544C",
      "override": "image/apng"
    }
  ]
}
```
 */

struct MimeDataTrack
{
	std::string overriden_name;
};

class MimeDataIdentifier
{
	std::vector< std::unique_ptr< MimeDataIdentifier > > m_children {};

	virtual bool pass(
		const std::byte* data,
		const std::size_t length,
		std::size_t& current_offset,
		MimeDataTrack& context ) const = 0;

  public:

	MimeDataIdentifier() = delete;
	MimeDataIdentifier( const Json::Value& value );

	virtual ~MimeDataIdentifier() = default;

	bool test( const std::byte* data, const std::size_t length, std::size_t& current_offset, MimeDataTrack& context )
		const;
};

class DataIdentifierSearch : public MimeDataIdentifier
{
	FGL_DELETE_COPY( DataIdentifierSearch );

	constexpr static std::int64_t NO_OFFSET { std::numeric_limits< std::int64_t >::max() };
	std::int64_t offset { NO_OFFSET };
	std::vector< std::byte > m_data {};

  public:

	bool passOffset(
		const std::byte* data,
		const std::size_t length,
		std::size_t& current_offset,
		[[maybe_unused]] MimeDataTrack& context ) const;

	bool passNoOffset(
		const std::byte* data,
		const std::size_t length,
		std::size_t& current_offset,
		[[maybe_unused]] MimeDataTrack& context ) const;

	bool pass(
		const std::byte* data,
		const std::size_t length,
		std::size_t& current_offset,
		[[maybe_unused]] MimeDataTrack& context ) const override
	{
		if ( offset == NO_OFFSET ) return passNoOffset( data, length, current_offset, context );

		return passOffset( data, length, current_offset, context );
	}

	DataIdentifierSearch() = delete;
	DataIdentifierSearch( const Json::Value& json );

	DataIdentifierSearch( DataIdentifierSearch&& other ) = default;
	DataIdentifierSearch& operator=( DataIdentifierSearch&& other ) = default;
};

struct DataIdentifierOverride final : public DataIdentifierSearch
{
	std::string override_name {};

	bool pass( const std::byte* data, const std::size_t length, std::size_t& current_offset, MimeDataTrack& context )
		const override;

	explicit DataIdentifierOverride( const Json::Value& json );
};

struct MimeIdentifier
{
	//! Name of the mime (example: "image/jpeg")
	std::string m_mime {};

	//! list of valid extensions
	std::vector< std::string > m_extensions {};

	enum Type
	{
		//! A data search
		Search,
		Override
	};

	bool fast_pass { false };

	std::vector< std::unique_ptr< MimeDataIdentifier > > m_identifiers {};

	MimeIdentifier() = delete;

	MimeIdentifier(
		std::string&& mime,
		std::vector< std::string >&& extensions,
		std::vector< std::unique_ptr< MimeDataIdentifier > >&& identifiers );

	bool test( const std::byte* data, std::size_t length, MimeDataTrack& context ) const;

	static MimeIdentifier loadFromFile( const std::filesystem::path& path );
	static MimeIdentifier loadFromJson( const Json::Value& json );
};

//! This mime type is used for unknown mime types
constexpr auto INVALID_MIME_NAME { "unknown/unknown" };

class MimeDatabase
{
	MimeDatabase();

	friend std::shared_ptr< MimeDatabase > getInstance();

	std::vector< MimeIdentifier > m_identifiers {};

	std::condition_variable update_condition {};
	std::mutex mutex {};
	std::atomic< bool > updating_flag {};
	std::atomic< int > active_counter {};

  public:

	std::expected< std::string, std::exception > scan( const std::byte* data, std::size_t length );

	std::expected< std::string, std::exception > scan( const std::vector< std::byte >& data )
	{
		return scan( data.data(), data.size() );
	}

	std::expected< std::string, std::exception > scan( const std::string_view& data )
	{
		return scan( reinterpret_cast< const std::byte* >( data.data() ), data.size() );
	}

	std::expected< std::string, std::exception > scanFile( const std::filesystem::path& path );

	//! Reloads all the 3rd party mime parsers
	void reloadMimeParsers();
};

std::shared_ptr< MimeDatabase > getInstance();
drogon::Task< MimeID > getIDForStr( std::string str, drogon::orm::DbClientPtr db );

} // namespace idhan::mime