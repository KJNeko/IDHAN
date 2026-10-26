#include "ApiClient.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <format>
#include <limits>
#include <memory>
#include <stdexcept>

namespace idhan::test
{

Json::Value tagBody( const TagPairs& pairs )
{
	Json::Value json { Json::arrayValue };

	for ( const auto& [ namespace_text, subtag_text ] : pairs )
	{
		Json::Value entry {};
		entry[ "namespace" ] = namespace_text;
		entry[ "subtag" ] = subtag_text;
		json.append( entry );
	}

	return json;
}

Json::Value aliasBody( const IdPairs& aliased_to_alias )
{
	Json::Value json { Json::arrayValue };

	for ( const auto& [ aliased_id, alias_id ] : aliased_to_alias )
	{
		Json::Value entry {};
		entry[ "aliased_id" ] = aliased_id;
		entry[ "alias_id" ] = alias_id;
		json.append( entry );
	}

	return json;
}

Json::Value parentBody( const IdPairs& parent_to_child )
{
	Json::Value json { Json::arrayValue };

	for ( const auto& [ parent_id, child_id ] : parent_to_child )
	{
		Json::Value entry {};
		entry[ "parent_id" ] = parent_id;
		entry[ "child_id" ] = child_id;
		json.append( entry );
	}

	return json;
}

Json::Value recordTagBody( const RecordIDs& record_ids, const TagIDs& tag_ids )
{
	Json::Value json {};
	json[ "records" ] = Json::Value { Json::arrayValue };
	json[ "tags" ] = Json::Value { Json::arrayValue };

	for ( const auto record_id : record_ids ) json[ "records" ].append( record_id );
	for ( const auto tag_id : tag_ids ) json[ "tags" ].append( tag_id );

	return json;
}

Json::Value recordTagSetBody( const RecordIDs& record_ids, const TagIDs& tag_ids )
{
	Json::Value json {};
	json[ "records" ] = Json::Value { Json::arrayValue };
	json[ "sets" ] = Json::Value { Json::arrayValue };

	Json::Value set { Json::arrayValue };
	for ( const auto tag_id : tag_ids ) set.append( tag_id );

	for ( const auto record_id : record_ids )
	{
		json[ "records" ].append( record_id );
		json[ "sets" ].append( set );
	}

	return json;
}

std::string hashFor( const int seed )
{
	constexpr std::string_view DIGITS { "0123456789abcdef" };

	std::string hex( 64, '0' );
	for ( std::size_t i = 0; i < 8; ++i )
		hex[ hex.size() - 1 - i ] = DIGITS[ static_cast< unsigned >( seed >> ( i * 4 ) ) & 0xFu ];

	return hex;
}

Json::Value domainBody( const std::string& name )
{
	Json::Value json {};
	json[ "name" ] = name;
	return json;
}

TagDomainID domainOf( const ApiResponse& response )
{
	return static_cast< TagDomainID >( response.json[ "tag_domain_id" ].asInt() );
}

class CurlRuntime
{
  public:

	CurlRuntime()
	{
		const auto result { curl_global_init( CURL_GLOBAL_DEFAULT ) };
		if ( result != CURLE_OK ) throw std::runtime_error( curl_easy_strerror( result ) );
	}

	~CurlRuntime() { curl_global_cleanup(); }
};

struct CurlResponse
{
	ApiResponse response;
	std::exception_ptr failure;
};

static std::size_t appendBody( char* data, const std::size_t size, const std::size_t count, void* userdata ) noexcept
{
	auto& transfer { *static_cast< CurlResponse* >( userdata ) };
	try
	{
		transfer.response.body.append( data, size * count );
		return size * count;
	}
	catch ( ... )
	{
		transfer.failure = std::current_exception();
		return 0;
	}
}

static std::size_t appendHeader( char* data, const std::size_t size, const std::size_t count, void* userdata ) noexcept
{
	auto& transfer { *static_cast< CurlResponse* >( userdata ) };
	try
	{
		const std::string_view line { data, size * count };
		if ( line.starts_with( "HTTP/" ) ) transfer.response.headers.clear();
		const auto separator { line.find( ':' ) };
		if ( separator == std::string_view::npos ) return size * count;

		std::string name { line.substr( 0, separator ) };
		std::ranges::transform(
			name, name.begin(), []( const unsigned char character ) { return std::tolower( character ); } );
		auto value { line.substr( separator + 1 ) };
		while ( !value.empty() && ( value.front() == ' ' || value.front() == '\t' ) ) value.remove_prefix( 1 );
		while ( !value.empty()
		        && ( value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t' ) )
			value.remove_suffix( 1 );
		transfer.response.headers.insert_or_assign( std::move( name ), std::string { value } );
		return size * count;
	}
	catch ( ... )
	{
		transfer.failure = std::current_exception();
		return 0;
	}
}

template < typename T >
static void setCurlOption( CURL* handle, const CURLoption option, const T value )
{
	const auto result { curl_easy_setopt( handle, option, value ) };
	if ( result != CURLE_OK ) throw std::runtime_error( curl_easy_strerror( result ) );
}

static std::string encodeQueryValue( CURL* handle, const std::string& value )
{
	if ( value.size() > static_cast< std::size_t >( std::numeric_limits< int >::max() ) )
		throw std::runtime_error( "Query parameter is too large for curl_easy_escape" );

	const std::unique_ptr< char, decltype( &curl_free ) > encoded {
		curl_easy_escape( handle, value.data(), static_cast< int >( value.size() ) ), &curl_free
	};
	if ( !encoded ) throw std::runtime_error( "Could not encode query parameter" );

	return encoded.get();
}

ApiClient::ApiClient( const std::uint16_t port, const long timeout_seconds, const long upload_timeout_seconds ) :
  m_base_url( std::format( "http://127.0.0.1:{}", port ) ),
  m_timeout_seconds( timeout_seconds ),
  m_upload_timeout_seconds( upload_timeout_seconds )
{
	if ( timeout_seconds <= 0 ) throw std::invalid_argument( "Request timeout must be positive" );

	if ( upload_timeout_seconds <= 0 ) throw std::invalid_argument( "Upload timeout must be positive" );

	static const CurlRuntime runtime;
}

ApiResponse ApiClient::sendRaw(
	const char* method,
	const std::string& path,
	const QueryParams& query,
	const std::string_view body,
	const char* content_type,
	const std::string& key,
	const long timeout_seconds )
{
	const std::unique_ptr< CURL, decltype( &curl_easy_cleanup ) > handle { curl_easy_init(), &curl_easy_cleanup };
	if ( !handle ) throw std::runtime_error( "Could not initialize HTTP request" );

	std::string url { m_base_url + path };
	bool first { !url.contains( '?' ) };
	for ( const auto& [ name, value ] : query )
	{
		url += first ? '?' : '&';
		first = false;
		url += encodeQueryValue( handle.get(), name ) + "=" + encodeQueryValue( handle.get(), value );
	}

	CurlResponse transfer;
	std::array< char, CURL_ERROR_SIZE > error {};
	setCurlOption( handle.get(), CURLOPT_URL, url.c_str() );
	setCurlOption( handle.get(), CURLOPT_CUSTOMREQUEST, method );
	setCurlOption( handle.get(), CURLOPT_HTTP_VERSION, static_cast< long >( CURL_HTTP_VERSION_1_1 ) );
	setCurlOption( handle.get(), CURLOPT_NOSIGNAL, 1L );
	setCurlOption( handle.get(), CURLOPT_TIMEOUT, timeout_seconds );
	// Loopback tests must connect directly even when the caller exports proxy settings.
	setCurlOption( handle.get(), CURLOPT_PROXY, "" );
	setCurlOption( handle.get(), CURLOPT_WRITEFUNCTION, &appendBody );
	setCurlOption( handle.get(), CURLOPT_WRITEDATA, &transfer );
	setCurlOption( handle.get(), CURLOPT_HEADERFUNCTION, &appendHeader );
	setCurlOption( handle.get(), CURLOPT_HEADERDATA, &transfer );
	setCurlOption( handle.get(), CURLOPT_ERRORBUFFER, error.data() );

	std::unique_ptr< curl_slist, decltype( &curl_slist_free_all ) > headers { nullptr, &curl_slist_free_all };
	for ( const auto& header :
	      { key.empty() ? std::string {} : "X-API-Key: " + key,
	        content_type ? std::string { "Content-Type: " } + content_type : std::string {} } )
	{
		if ( header.empty() ) continue;
		auto* appended { curl_slist_append( headers.get(), header.c_str() ) };
		if ( !appended ) throw std::runtime_error( "Could not allocate HTTP headers" );
		(void)headers.release();
		headers.reset( appended );
	}
	setCurlOption( handle.get(), CURLOPT_HTTPHEADER, headers.get() );
	if ( content_type )
	{
		setCurlOption( handle.get(), CURLOPT_POSTFIELDS, body.empty() ? "" : body.data() );
		setCurlOption( handle.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast< curl_off_t >( body.size() ) );
	}

	const auto result { curl_easy_perform( handle.get() ) };
	if ( transfer.failure ) std::rethrow_exception( transfer.failure );
	if ( result != CURLE_OK )
		throw std::runtime_error(
			std::format( "Request to {} failed: {}", path, error[ 0 ] ? error.data() : curl_easy_strerror( result ) ) );

	const auto status_result { curl_easy_getinfo( handle.get(), CURLINFO_RESPONSE_CODE, &transfer.response.status ) };
	if ( status_result != CURLE_OK ) throw std::runtime_error( curl_easy_strerror( status_result ) );

	Json::CharReaderBuilder builder;
	const std::unique_ptr< Json::CharReader > reader { builder.newCharReader() };
	const auto& text { transfer.response.body };
	Json::Value parsed;
	std::string errors;
	if ( reader->parse( text.data(), text.data() + text.size(), &parsed, &errors ) )
		transfer.response.json = std::move( parsed );
	return std::move( transfer.response );
}

ApiResponse ApiClient::send(
	const char* method,
	const std::string& path,
	const QueryParams& query,
	const Json::Value* const body,
	const std::string& key )
{
	Json::StreamWriterBuilder writer;
	writer[ "indentation" ] = "";
	const auto text { body ? Json::writeString( writer, *body ) : std::string {} };
	return sendRaw( method, path, query, text, body ? "application/json" : nullptr, key, m_timeout_seconds );
}

void ApiClient::authenticate()
{
	const auto response { send( "GET", "/generate_api_key", {}, nullptr, {} ) };

	if ( response.status != 200 )
		throw std::runtime_error(
			std::format(
				"Could not mint an API key, the server answered {}: {}",
				static_cast< int >( response.status ),
				response.body ) );

	m_key = response.json[ "key" ].asString();

	if ( m_key.empty() ) throw std::runtime_error( "The server minted an empty API key" );
}

ApiResponse ApiClient::get( const std::string& path, const QueryParams& query )
{
	return send( "GET", path, query, nullptr, m_key );
}

ApiResponse ApiClient::post( const std::string& path, const Json::Value& body, const QueryParams& query )
{
	return send( "POST", path, query, &body, m_key );
}

ApiResponse ApiClient::patch( const std::string& path, const Json::Value& body )
{
	return send( "PATCH", path, {}, &body, m_key );
}

ApiResponse ApiClient::postWithoutBody( const std::string& path, const QueryParams& query )
{
	return send( "POST", path, query, nullptr, m_key );
}

ApiResponse ApiClient::del( const std::string& path, const QueryParams& query )
{
	return send( "DELETE", path, query, nullptr, m_key );
}

ApiResponse ApiClient::getWithKey( const std::string& path, const std::string& key )
{
	return send( "GET", path, {}, nullptr, key );
}

ApiResponse ApiClient::postWithKey( const std::string& path, const Json::Value& body, const std::string& key )
{
	return send( "POST", path, {}, &body, key );
}

ApiResponse ApiClient::postOctets( const std::string& path, const std::string_view body, const QueryParams& query )
{
	return sendRaw( "POST", path, query, body, "application/octet-stream", m_key, m_upload_timeout_seconds );
}

TagDomainID ApiClient::createDomain( const std::string& name )
{
	const auto response { post( "/tags/domain/create", domainBody( name ) ) };

	if ( response.status != 200 )
		throw std::runtime_error( std::format( "Could not create tag domain '{}': {}", name, response.body ) );

	return domainOf( response );
}

std::vector< TagID > ApiClient::createTags( const TagPairs& pairs )
{
	const auto response { post( "/tags/create", tagBody( pairs ) ) };

	if ( response.status != 200 ) throw std::runtime_error( std::format( "Could not create tags: {}", response.body ) );

	std::vector< TagID > ids {};
	ids.reserve( response.json.size() );

	for ( const auto& entry : response.json ) ids.emplace_back( entry[ "tag_id" ].asInt() );

	return ids;
}

TagID ApiClient::createTag( const std::string& namespace_text, const std::string& subtag_text )
{
	return createTags( { { namespace_text, subtag_text } } ).at( 0 );
}

ApiResponse ApiClient::createAliases( const TagDomainID tag_domain_id, const IdPairs& aliased_to_alias )
{
	return post(
		"/tags/alias/create", aliasBody( aliased_to_alias ), { { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

ApiResponse ApiClient::removeAliases( const TagDomainID tag_domain_id, const IdPairs& aliased_to_alias )
{
	return post(
		"/tags/alias/remove", aliasBody( aliased_to_alias ), { { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

ApiResponse ApiClient::createParents( const TagDomainID tag_domain_id, const IdPairs& parent_to_child )
{
	return post(
		"/tags/parents/create",
		parentBody( parent_to_child ),
		{ { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

ApiResponse ApiClient::removeParents( const TagDomainID tag_domain_id, const IdPairs& parent_to_child )
{
	return post(
		"/tags/parents/remove",
		parentBody( parent_to_child ),
		{ { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

RecordIDs ApiClient::createRecords( const std::vector< int >& seeds )
{
	Json::Value hashes { Json::arrayValue };
	for ( const auto seed : seeds ) hashes.append( hashFor( seed ) );

	Json::Value body {};
	body[ "sha256" ] = hashes;

	const auto response { post( "/records/create", body ) };

	if ( response.status != 200 )
		throw std::runtime_error( std::format( "Could not create records: {}", response.body ) );

	RecordIDs ids {};
	ids.reserve( response.json.size() );

	for ( const auto& entry : response.json ) ids.emplace_back( entry.asInt() );

	return ids;
}

RecordID ApiClient::createRecord( const int seed )
{
	return createRecords( { seed } ).at( 0 );
}

ApiResponse ApiClient::addTags( const TagDomainID tag_domain_id, const RecordIDs& record_ids, const TagIDs& tag_ids )
{
	return post(
		"/records/tags/add",
		recordTagBody( record_ids, tag_ids ),
		{ { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

ApiResponse ApiClient::removeTags( const TagDomainID tag_domain_id, const RecordIDs& record_ids, const TagIDs& tag_ids )
{
	return post(
		"/records/tags/remove",
		recordTagSetBody( record_ids, tag_ids ),
		{ { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

RecordIDs ApiClient::search( const TagDomainID tag_domain_id, const TagIDs& tag_ids )
{
	QueryParams query { { "tag_domains", std::to_string( tag_domain_id ) } };
	for ( const auto tag_id : tag_ids ) query.emplace_back( "tag_ids", std::to_string( tag_id ) );

	const auto response { get( "/search", query ) };

	if ( response.status != 200 ) throw std::runtime_error( std::format( "Search failed: {}", response.body ) );

	RecordIDs ids {};
	ids.reserve( response.json.size() );

	for ( const auto& entry : response.json ) ids.emplace_back( entry.asInt() );

	std::sort( ids.begin(), ids.end() );

	return ids;
}

} // namespace idhan::test
