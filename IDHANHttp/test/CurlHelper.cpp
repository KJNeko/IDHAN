#include "CurlHelper.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>

namespace idhan::test
{

static bool iequals( const std::string_view lhs, const std::string_view rhs )
{
	const auto same = []( const char l, const char r )
	{ return std::tolower( static_cast< unsigned char >( l ) ) == std::tolower( static_cast< unsigned char >( r ) ); };

	return std::ranges::equal( lhs, rhs, same );
}

static std::string_view trim( std::string_view text )
{
	constexpr std::string_view whitespace { " \t" };

	while ( !text.empty() && whitespace.contains( text.front() ) ) text.remove_prefix( 1 );
	while ( !text.empty() && whitespace.contains( text.back() ) ) text.remove_suffix( 1 );

	return text;
}

std::optional< std::string_view > CurlResponse::header( const std::string_view name ) const
{
	for ( const auto& [ key, value ] : headers )
	{
		if ( iequals( key, name ) ) return std::string_view { value };
	}

	return std::nullopt;
}

std::vector< std::string_view > CurlResponse::headerValues( const std::string_view name ) const
{
	std::vector< std::string_view > values {};

	for ( const auto& [ key, value ] : headers )
	{
		if ( iequals( key, name ) ) values.emplace_back( value );
	}

	return values;
}

struct CurlGlobal
{
	CurlGlobal()
	{
		if ( const CURLcode code = curl_global_init( CURL_GLOBAL_DEFAULT ); code != CURLE_OK )
			throw CurlError( std::format( "curl_global_init failed: {}", curl_easy_strerror( code ) ) );
	}

	~CurlGlobal() { curl_global_cleanup(); }

	CurlGlobal( const CurlGlobal& ) = delete;
	CurlGlobal& operator=( const CurlGlobal& ) = delete;
};

static void initGlobal()
{
	static const CurlGlobal global {};
}

static std::size_t writeBody( char* data, const std::size_t size, const std::size_t count, void* userdata )
{
	const std::size_t total { size * count };
	static_cast< std::string* >( userdata )->append( data, total );
	return total;
}

static std::size_t writeHeader( char* data, const std::size_t size, const std::size_t count, void* userdata )
{
	const std::size_t total { size * count };
	auto& response { *static_cast< CurlResponse* >( userdata ) };
	auto& headers { response.headers };

	std::string_view line { data, total };
	while ( line.ends_with( '\n' ) || line.ends_with( '\r' ) ) line.remove_suffix( 1 );

	// a 1xx or redirect block is followed by another status line; only the final block's headers are kept
	if ( line.starts_with( "HTTP/" ) )
	{
		const auto separator { line.find( ' ' ) };
		if ( separator != std::string_view::npos )
		{
			unsigned status {};
			const auto parsed { std::from_chars( line.data() + separator + 1, line.data() + line.size(), status ) };
			if ( parsed.ec == std::errc {} && status >= 100 && status < 200 )
				response.informational_statuses.push_back( status );
		}
		headers.clear();
		return total;
	}

	const auto colon { line.find( ':' ) };
	if ( colon == std::string_view::npos ) return total;

	headers.emplace_back( trim( line.substr( 0, colon ) ), trim( line.substr( colon + 1 ) ) );
	return total;
}

static bool hasHeaderNamed( const std::vector< std::string >& headers, const std::string_view name )
{
	for ( const std::string_view line : headers )
	{
		const auto colon { line.find( ':' ) };
		if ( colon == std::string_view::npos ) continue;
		if ( iequals( trim( line.substr( 0, colon ) ), name ) ) return true;
	}

	return false;
}

using SList = std::unique_ptr< curl_slist, decltype( []( curl_slist* list ) { curl_slist_free_all( list ); } ) >;

static SList buildHeaders( const std::vector< std::string >& headers )
{
	SList list { nullptr };

	const auto append = [ &list ]( const std::string& line )
	{
		curl_slist* appended { curl_slist_append( list.get(), line.c_str() ) };
		if ( appended == nullptr ) throw CurlError( std::format( "Failed to append header \"{}\"", line ) );
		static_cast< void >( list.release() );
		list.reset( appended );
	};

	for ( const std::string& line : headers ) append( line );

	// curl adds "Expect: 100-continue" above 1 KiB and guesses a Content-Type; a test sends what it asked to send
	if ( !hasHeaderNamed( headers, "Expect" ) ) append( std::string { "Expect:" } );
	if ( !hasHeaderNamed( headers, "Content-Type" ) ) append( std::string { "Content-Type:" } );

	return list;
}

CurlResponse send( const CurlRequest& request )
{
	initGlobal();

	const std::unique_ptr< CURL, decltype( []( CURL* handle ) { curl_easy_cleanup( handle ); } ) > handle {
		curl_easy_init()
	};
	if ( !handle ) throw CurlError( "curl_easy_init failed" );

	CURL* const easy { handle.get() };

	CurlResponse response {};
	std::array< char, CURL_ERROR_SIZE > error {};

	curl_easy_setopt( easy, CURLOPT_URL, request.url.c_str() );
	curl_easy_setopt( easy, CURLOPT_ERRORBUFFER, error.data() );
	curl_easy_setopt( easy, CURLOPT_TIMEOUT_MS, static_cast< long >( request.timeout.count() ) );
	curl_easy_setopt(
		easy, CURLOPT_EXPECT_100_TIMEOUT_MS, static_cast< long >( request.expect_continue_timeout.count() ) );
	curl_easy_setopt( easy, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L );
	curl_easy_setopt( easy, CURLOPT_NOSIGNAL, 1L );
	curl_easy_setopt( easy, CURLOPT_WRITEFUNCTION, &writeBody );
	curl_easy_setopt( easy, CURLOPT_WRITEDATA, &response.body );
	curl_easy_setopt( easy, CURLOPT_HEADERFUNCTION, &writeHeader );
	curl_easy_setopt( easy, CURLOPT_HEADERDATA, &response );

	const bool is_head { iequals( request.method, "HEAD" ) };
	const bool is_get { iequals( request.method, "GET" ) };

	if ( is_head )
		curl_easy_setopt( easy, CURLOPT_NOBODY, 1L );
	else if ( !is_get )
		curl_easy_setopt( easy, CURLOPT_CUSTOMREQUEST, request.method.c_str() );

	if ( !is_head && ( !is_get || !request.body.empty() ) )
	{
		curl_easy_setopt( easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast< curl_off_t >( request.body.size() ) );
		curl_easy_setopt( easy, CURLOPT_POSTFIELDS, request.body.data() );
	}

	const SList headers { buildHeaders( request.headers ) };
	if ( headers ) curl_easy_setopt( easy, CURLOPT_HTTPHEADER, headers.get() );

	if ( const CURLcode code = curl_easy_perform( easy ); code != CURLE_OK )
	{
		const std::string_view detail { error.data() };
		const std::string_view reason { detail.empty() ? std::string_view { curl_easy_strerror( code ) } : detail };
		throw CurlError( std::format( "{} {} failed: {}", request.method, request.url, reason ) );
	}

	curl_easy_getinfo( easy, CURLINFO_RESPONSE_CODE, &response.status );
	curl_off_t uploaded {};
	curl_easy_getinfo( easy, CURLINFO_SIZE_UPLOAD_T, &uploaded );
	response.uploaded_bytes = static_cast< std::uint64_t >( uploaded );

	return response;
}

static CurlResponse sendWithBody(
	std::string url,
	std::string method,
	std::string body,
	std::string content_type,
	std::vector< std::string > headers )
{
	if ( !content_type.empty() && !hasHeaderNamed( headers, "Content-Type" ) )
		headers.emplace_back( std::format( "Content-Type: {}", content_type ) );

	return send(
		CurlRequest {
			.url = std::move( url ),
			.method = std::move( method ),
			.headers = std::move( headers ),
			.body = std::move( body ) } );
}

CurlResponse get( std::string url, std::vector< std::string > headers )
{
	return send( CurlRequest { .url = std::move( url ), .method = "GET", .headers = std::move( headers ) } );
}

CurlResponse head( std::string url, std::vector< std::string > headers )
{
	return send( CurlRequest { .url = std::move( url ), .method = "HEAD", .headers = std::move( headers ) } );
}

CurlResponse deleteRequest( std::string url, std::vector< std::string > headers )
{
	return send( CurlRequest { .url = std::move( url ), .method = "DELETE", .headers = std::move( headers ) } );
}

CurlResponse post( std::string url, std::string body, std::string content_type, std::vector< std::string > headers )
{
	return sendWithBody( std::move( url ), "POST", std::move( body ), std::move( content_type ), std::move( headers ) );
}

CurlResponse put( std::string url, std::string body, std::string content_type, std::vector< std::string > headers )
{
	return sendWithBody( std::move( url ), "PUT", std::move( body ), std::move( content_type ), std::move( headers ) );
}

CurlResponse postJson( std::string url, std::string json, std::vector< std::string > headers )
{
	return sendWithBody( std::move( url ), "POST", std::move( json ), "application/json", std::move( headers ) );
}

CurlResponse putJson( std::string url, std::string json, std::vector< std::string > headers )
{
	return sendWithBody( std::move( url ), "PUT", std::move( json ), "application/json", std::move( headers ) );
}

CurlResponse sendBytes(
	std::string url,
	std::string method,
	const std::span< const std::byte > bytes,
	std::string content_type,
	std::vector< std::string > headers )
{
	std::string body {};
	body.resize( bytes.size() );
	std::memcpy( body.data(), bytes.data(), bytes.size() );

	return sendWithBody(
		std::move( url ), std::move( method ), std::move( body ), std::move( content_type ), std::move( headers ) );
}

CurlResponse sendFile(
	std::string url,
	std::string method,
	const std::filesystem::path& path,
	std::string content_type,
	std::vector< std::string > headers )
{
	std::ifstream file { path, std::ios::binary };
	if ( !file ) throw CurlError( std::format( "Failed to open \"{}\"", path.string() ) );

	std::string body { std::istreambuf_iterator< char > { file }, std::istreambuf_iterator< char > {} };
	if ( !file ) throw CurlError( std::format( "Failed to read \"{}\"", path.string() ) );

	return sendWithBody(
		std::move( url ), std::move( method ), std::move( body ), std::move( content_type ), std::move( headers ) );
}

CurlResponse getRange(
	std::string url,
	const std::uint64_t first,
	std::optional< std::uint64_t > last,
	std::vector< std::string > headers )
{
	headers.emplace_back( rangeHeader( first, last ) );
	return get( std::move( url ), std::move( headers ) );
}

CurlResponse getSuffixRange( std::string url, const std::uint64_t length, std::vector< std::string > headers )
{
	headers.emplace_back( suffixRangeHeader( length ) );
	return get( std::move( url ), std::move( headers ) );
}

std::string rangeHeader( const std::uint64_t first, const std::optional< std::uint64_t > last )
{
	if ( last.has_value() ) return std::format( "Range: bytes={}-{}", first, last.value() );
	return std::format( "Range: bytes={}-", first );
}

std::string suffixRangeHeader( const std::uint64_t length )
{
	return std::format( "Range: bytes=-{}", length );
}

std::string urlEncode( const std::string_view value )
{
	initGlobal();

	const std::unique_ptr< CURL, decltype( []( CURL* handle ) { curl_easy_cleanup( handle ); } ) > easy {
		curl_easy_init()
	};
	if ( !easy ) throw CurlError( "curl_easy_init failed" );

	const std::unique_ptr< char, decltype( []( char* escaped ) { curl_free( escaped ); } ) > escaped {
		curl_easy_escape( easy.get(), value.data(), static_cast< int >( value.size() ) )
	};
	if ( !escaped ) throw CurlError( std::format( "Failed to encode \"{}\"", value ) );

	return std::string { escaped.get() };
}

std::string urlWithQuery(
	const std::string_view url,
	const std::vector< std::pair< std::string, std::string > >& params )
{
	if ( params.empty() ) return std::string { url };

	std::string result { url };
	result += url.contains( '?' ) ? '&' : '?';

	for ( std::size_t i = 0; i < params.size(); ++i )
	{
		const auto& [ key, value ] = params[ i ];
		if ( i != 0 ) result += '&';
		result += urlEncode( key );
		result += '=';
		result += urlEncode( value );
	}

	return result;
}

std::string localUrl( const std::uint16_t port, const std::string_view path )
{
	return std::format( "http://127.0.0.1:{}{}{}", port, path.starts_with( '/' ) ? "" : "/", path );
}

} // namespace idhan::test
