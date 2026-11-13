#include "http/LaneShard.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <utility>

#include "HostCallback.hpp"
#include "IDHANDownloader/http/Bandwidth.hpp"
#include "IDHANDownloader/http/DirectResponse.hpp"
#include "urls/URLUtils.hpp"
#include "http/IoPool.hpp"
#include "http/Lane.hpp"
#include "http/UserAgent.hpp"
#include "logging/format_ns.hpp"

namespace idhan::downloader
{

bool isSensitiveHeader( std::string name )
{
	std::ranges::transform(
		name, name.begin(), []( const unsigned char value ) { return static_cast< char >( std::tolower( value ) ); } );

	return name == "authorization" || name == "proxy-authorization" || name == "cookie" || name == "set-cookie"
	    || name == "x-api-key";
}

static std::string describeHeaders( const HttpHeaders& headers )
{
	std::string described {};

	for ( const auto& [ name, value ] : headers )
	{
		if ( !described.empty() ) described += ", ";

		described += format_ns::format( "{}: {}", name, isSensitiveHeader( name ) ? "<redacted>" : value );
	}

	return described;
}

static bool isRedirectStatus( const std::int32_t status )
{
	return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static bool redirecting( const LaneShard::Active& active )
{
	return active.request.options.follow_redirects && isRedirectStatus( active.response.status );
}

static bool successful( const LaneShard::Active& active )
{
	return active.response.status >= 200 && active.response.status < 300;
}

//! Only a successful final hop delivers bytes; everything else hands the sink back untouched.
static bool preservingSink( const LaneShard::Active& active )
{
	return redirecting( active ) || !successful( active );
}

static long curlHttpVersion( const HttpVersion version )
{
	switch ( version )
	{
		case HttpVersion::HTTP_1_1:
			return CURL_HTTP_VERSION_1_1;
		case HttpVersion::HTTP_2:
			return CURL_HTTP_VERSION_2TLS;
		case HttpVersion::HTTP_3:
			return CURL_HTTP_VERSION_3;
	}

	return CURL_HTTP_VERSION_1_1;
}

static HttpVersion available( const HttpVersion wanted )
{
	if ( wanted == HttpVersion::HTTP_3 && !IoPool::supportsHttp3() ) return available( HttpVersion::HTTP_2 );
	if ( wanted == HttpVersion::HTTP_2 && !IoPool::supportsHttp2() ) return HttpVersion::HTTP_1_1;

	return wanted;
}

LaneShard::LaneShard( std::weak_ptr< Lane > lane, IoThread& thread, const std::size_t host_connections ) :
  m_lane( std::move( lane ) ),
  m_thread( thread ),
  m_multi( curl_multi_init() )
{
	if ( m_multi == nullptr ) return;

	curl_multi_setopt( m_multi.get(), CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX );
	curl_multi_setopt( m_multi.get(), CURLMOPT_MAX_HOST_CONNECTIONS, static_cast< long >( host_connections ) );
}

LaneShard::~LaneShard()
{
	shutdown();
}

void LaneShard::release( Active& active )
{
	const auto parsed_e { ada::parse< ada::url_aggregator >( active.request.url ) };
	if ( parsed_e.has_value() )
	{
		std::string host { parsed_e.value().get_hostname() };
		if ( host.ends_with( '.' ) ) host.pop_back();

		curl_off_t received {};
		long header_bytes {};
		curl_easy_getinfo( active.easy.get(), CURLINFO_SIZE_DOWNLOAD_T, &received );
		curl_easy_getinfo( active.easy.get(), CURLINFO_HEADER_SIZE, &header_bytes );
		recordBandwidth(
			{ std::move( host ),
			  std::chrono::duration_cast< std::chrono::seconds >( std::chrono::system_clock::now().time_since_epoch() )
			      .count(),
			  static_cast< std::int64_t >( received ) + header_bytes } );
	}

	if ( active.request.progress )
		active.request.progress->phase =
			active.request.buffered_sink ? NetworkActivity::Phase::STORAGE : NetworkActivity::Phase::FINISHED;

	active.alive->store( false );
	curl_multi_remove_handle( m_multi.get(), active.easy.get() );
	active.header_list.reset();
	active.easy.reset();
}

void LaneShard::shutdown()
{
	while ( !m_active.empty() )
	{
		auto node { m_active.extract( m_active.begin() ) };
		Active& active { *node.mapped() };

		if ( active.request.buffered_sink ) (void)hostCallback( [ & ] { active.request.buffered_sink->abort(); } );

		release( active );
		m_assigned.fetch_sub( 1, std::memory_order_relaxed );

		if ( active.callback )
			active.callback(
				std::unexpected(
					TransferError {
						.code = TransferErrorCode::SHUTDOWN, .message = "The downloader is shutting down" } ) );
	}
}

int LaneShard::debugCallback( CURL* easy, const curl_infotype type, char* data, const std::size_t size, void* user )
{
	// Observe outgoing headers only; never print libcurl's debug contents (which include secrets).
	if ( type != CURLINFO_HEADER_OUT ) return 0;

	if ( user == nullptr ) return 0;

	auto* active { static_cast< Active* >( user ) };
	if ( easy != active->easy.get() ) return 0;

	// A proxy tunnel is setup, not the HTTP request to the destination.
	if ( std::string_view { data, size }.starts_with( "CONNECT " ) ) return 0;

	if ( !active->request.progress ) return 0;

	auto& progress { *active->request.progress };
	if ( progress.sent_at.load() != 0 ) return 0;

	progress.stampSent();
	return 0;
}

void LaneShard::recordProgress( Active& active, const std::size_t bytes )
{
	auto& progress { *active.request.progress };
	if ( active.response.bytes == 0 )
	{
		curl_off_t length {};
		const bool known {
			curl_easy_getinfo( active.easy.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length ) == CURLE_OK
			&& length > 0
		};
		progress.expected = known ? static_cast< std::uint64_t >( length ) : 0;
	}

	progress.received += bytes;
}

std::size_t LaneShard::writeCallback( char* data, const std::size_t size, const std::size_t count, void* user )
{
	const std::size_t total { size * count };
	auto* active { static_cast< Active* >( user ) };
	if ( active->request.buffered_sink && !preservingSink( *active )
	     && !active->request.buffered_sink->canWrite( total ) )
	{
		active->paused = true;
		if ( active->request.progress ) active->request.progress->phase = NetworkActivity::Phase::STORAGE;

		return CURL_WRITEFUNC_PAUSE;
	}
	if ( active->request.network ) active->request.network->received( total );

	if ( active->request.progress ) recordProgress( *active, total );

	active->response.bytes += total;
	const auto lane { active->shard->m_lane.lock() };

	if ( lane ) lane->onBytesReceived( total );

	if ( active->request.buffered_sink && active->preview.size() < HTTP_BODY_LOG_LIMIT )
		active->preview.append( data, std::min( total, HTTP_BODY_LOG_LIMIT - active->preview.size() ) );

	if ( active->request.buffered_sink && preservingSink( *active ) ) return total;

	if ( active->request.buffered_sink )
	{
		const auto written { hostOperation(
			[ & ]
			{
				return active->request.buffered_sink->enqueue(
					std::span { reinterpret_cast< const std::byte* >( data ), total } );
			} ) };

		if ( !written )
		{
			active->sink_error = written.error();
			return 0;
		}

		return total;
	}

	const auto limit { active->request.options.max_response_bytes };

	if ( limit != 0 && active->response.body.size() + total > limit )
	{
		active->over_limit = true;
		return 0;
	}

	active->response.body.append( data, total );
	return total;
}

std::size_t LaneShard::headerCallback( char* data, const std::size_t size, const std::size_t count, void* user )
{
	const std::size_t total { size * count };
	auto* active { static_cast< Active* >( user ) };
	std::string_view line { data, total };

	while ( !line.empty() && ( line.back() == '\r' || line.back() == '\n' ) ) line.remove_suffix( 1 );

	if ( line.empty() && active->response.status == 404 && active->request.options.stop_on_not_found )
	{
		active->stopped_on_not_found = true;
		return 0;
	}

	if ( line.starts_with( "HTTP/" ) )
	{
		active->response.headers.clear();
		active->response.body.clear();
		active->preview.clear();
		active->response.bytes = 0;
		active->response.status = 0;

		if ( const auto space { line.find( ' ' ) }; space != std::string_view::npos )
		{
			const std::string_view rest { line.substr( space + 1 ) };
			int status {};

			if ( std::from_chars( rest.data(), rest.data() + rest.size(), status ).ec == std::errc {} )
				active->response.status = status;
		}

		return total;
	}

	const auto separator { line.find( ':' ) };

	if ( separator == std::string_view::npos ) return total;

	const std::string_view name { line.substr( 0, separator ) };
	std::string_view value { line.substr( separator + 1 ) };

	while ( !value.empty() && ( value.front() == ' ' || value.front() == '\t' ) ) value.remove_prefix( 1 );

	active->response.headers.add( std::string { name }, std::string { value } );
	return total;
}

int LaneShard::progressCallback( void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t )
{
	const auto* active { static_cast< Active* >( user ) };
	return active->request.cancellation && active->request.cancellation->load() ? 1 : 0;
}

void LaneShard::start( TransferRequest request, TransferCallback callback )
{
	if ( request.cancellation && request.cancellation->load() )
	{
		if ( request.buffered_sink ) (void)hostCallback( [ & ] { request.buffered_sink->abort(); } );

		callback(
			std::unexpected(
				TransferError { .code = TransferErrorCode::CANCELLED, .message = "Transfer cancelled" } ) );
		m_assigned.fetch_sub( 1, std::memory_order_relaxed );
		if ( const auto lane { m_lane.lock() } ) lane->onTransferFinished();
		return;
	}

	auto active { std::make_unique< Active >() };
	active->shard = this;
	active->request = std::move( request );
	active->callback = std::move( callback );
	active->easy.reset( curl_easy_init() );

	if ( active->easy == nullptr )
	{
		if ( active->request.buffered_sink ) (void)hostCallback( [ & ] { active->request.buffered_sink->abort(); } );

		active->callback(
			std::unexpected(
				TransferError { .code = TransferErrorCode::TRANSPORT, .message = "curl_easy_init failed" } ) );
		m_assigned.fetch_sub( 1, std::memory_order_relaxed );
		if ( const auto lane { m_lane.lock() } ) lane->onTransferFinished();

		return;
	}

	CURL* easy { active->easy.get() };
	const TransferOptions& options { active->request.options };
	const auto version { available( options.http_version.value_or( HttpVersion::HTTP_3 ) ) };

	curl_easy_setopt( easy, CURLOPT_URL, active->request.url.c_str() );
	curl_easy_setopt( easy, CURLOPT_ERRORBUFFER, active->error.data() );
	curl_easy_setopt( easy, CURLOPT_PRIVATE, active.get() );
	curl_easy_setopt( easy, CURLOPT_DEBUGFUNCTION, debugCallback );
	curl_easy_setopt( easy, CURLOPT_DEBUGDATA, active.get() );
	curl_easy_setopt( easy, CURLOPT_VERBOSE, 1L );
	curl_easy_setopt( easy, CURLOPT_NOSIGNAL, 1L );
	curl_easy_setopt( easy, CURLOPT_HTTP_VERSION, curlHttpVersion( version ) );
	curl_easy_setopt( easy, CURLOPT_FOLLOWLOCATION, 0L );
	curl_easy_setopt( easy, CURLOPT_ACCEPT_ENCODING, "" );
	curl_easy_setopt( easy, CURLOPT_CONNECTTIMEOUT_MS, 15000L );
	curl_easy_setopt( easy, CURLOPT_WRITEFUNCTION, writeCallback );
	curl_easy_setopt( easy, CURLOPT_WRITEDATA, active.get() );
	curl_easy_setopt( easy, CURLOPT_HEADERFUNCTION, headerCallback );
	curl_easy_setopt( easy, CURLOPT_HEADERDATA, active.get() );
	curl_easy_setopt( easy, CURLOPT_NOPROGRESS, 0L );
	curl_easy_setopt( easy, CURLOPT_XFERINFOFUNCTION, progressCallback );
	curl_easy_setopt( easy, CURLOPT_XFERINFODATA, active.get() );

	if ( options.timeout_ms > 0 ) curl_easy_setopt( easy, CURLOPT_TIMEOUT_MS, options.timeout_ms );

	const std::string& user_agent { options.user_agent.empty() ? idhan_user_agent : options.user_agent };
	curl_easy_setopt( easy, CURLOPT_USERAGENT, user_agent.c_str() );

	switch ( active->request.method )
	{
		case HttpMethod::GET:
			curl_easy_setopt( easy, CURLOPT_HTTPGET, 1L );
			break;
		case HttpMethod::HEAD:
			curl_easy_setopt( easy, CURLOPT_NOBODY, 1L );
			break;
		case HttpMethod::POST:
		case HttpMethod::PUT:
		case HttpMethod::PATCH:
		case HttpMethod::DELETE:
		case HttpMethod::OPTIONS:
			{
				const std::string_view name { httpMethodName( active->request.method ) };
				curl_easy_setopt( easy, CURLOPT_CUSTOMREQUEST, std::string { name }.c_str() );
				curl_easy_setopt( easy, CURLOPT_POSTFIELDS, active->request.body.data() );
				curl_easy_setopt(
					easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast< curl_off_t >( active->request.body.size() ) );
				break;
			}
	}

	for ( const auto& [ name, value ] : active->request.headers )
		if ( !appendHeader( active->header_list, format_ns::format( "{}: {}", name, value ).c_str() ) ) break;

	appendHeader( active->header_list, "Expect:" );

	curl_easy_setopt( easy, CURLOPT_HTTPHEADER, active->header_list.get() );

	if ( const CURLMcode code { curl_multi_add_handle( m_multi.get(), easy ) }; code != CURLM_OK )
	{
		if ( active->request.buffered_sink ) (void)hostCallback( [ & ] { active->request.buffered_sink->abort(); } );

		active->callback(
			std::unexpected(
				TransferError { .code = TransferErrorCode::TRANSPORT, .message = curl_multi_strerror( code ) } ) );

		m_assigned.fetch_sub( 1, std::memory_order_relaxed );
		if ( const auto lane { m_lane.lock() } ) lane->onTransferFinished();

		return;
	}

	if ( spdlog::should_log( spdlog::level::trace ) )
		spdlog::trace(
			"downloader http: dispatching {} {} [{}]",
			httpMethodName( active->request.method ),
			detail::redactUrlQuery( active->request.url, active->request.sensitive_query ),
			describeHeaders( active->request.headers ) );

	if ( active->request.buffered_sink )
	{
		active->request.buffered_sink->setResumeCallback(
			[ weak = weak_from_this(), easy, alive = active->alive ]
			{
				const auto shard { weak.lock() };
				if ( !shard || !alive->load() ) return;

				shard->thread().post( [ shard, easy, alive ] { shard->resume( easy, alive ); } );
			} );
	}
	m_active.emplace( easy, std::move( active ) );
}

void LaneShard::resume( CURL* easy, std::shared_ptr< std::atomic_bool > alive )
{
	if ( !alive->load() ) return;

	const auto found { m_active.find( easy ) };
	if ( found == m_active.end() || !found->second->paused ) return;

	found->second->paused = false;
	if ( found->second->request.progress ) found->second->request.progress->phase = NetworkActivity::Phase::DOWNLOADING;

	curl_easy_pause( easy, CURLPAUSE_CONT );
	onProgress();
}

void LaneShard::onProgress()
{
	int remaining {};

	while ( CURLMsg * message { curl_multi_info_read( m_multi.get(), &remaining ) } )
	{
		if ( message->msg != CURLMSG_DONE ) continue;

		finish( message->easy_handle, message->data.result );
	}
}

static std::string filenameFor( const std::string_view url, const HttpHeaders& headers )
{
	const std::string_view disposition { headers.get( "content-disposition" ) };

	if ( const auto marker { disposition.find( "filename=" ) }; marker != std::string_view::npos )
	{
		std::string_view name { disposition.substr( marker + 9 ) };

		if ( const auto end { name.find( ';' ) }; end != std::string_view::npos ) name = name.substr( 0, end );
		if ( name.size() >= 2 && name.front() == '"' && name.back() == '"' ) name = name.substr( 1, name.size() - 2 );
		if ( !name.empty() ) return std::string { name };
	}

	std::string_view path { url };

	if ( const auto query { path.find( '?' ) }; query != std::string_view::npos ) path = path.substr( 0, query );

	const auto slash { path.rfind( '/' ) };

	return slash == std::string_view::npos ? std::string {} : std::string { path.substr( slash + 1 ) };
}

void LaneShard::finish( CURL* easy, const CURLcode code )
{
	const auto found { m_active.find( easy ) };

	if ( found == m_active.end() ) return;

	auto node { m_active.extract( found ) };
	Active& active { *node.mapped() };

	if ( active.request.progress ) active.request.progress->stampReceived();

	long status {};
	curl_easy_getinfo( easy, CURLINFO_RESPONSE_CODE, &status );

	const char* effective_url {};
	curl_easy_getinfo( easy, CURLINFO_EFFECTIVE_URL, &effective_url );
	std::string final_url { effective_url != nullptr ? effective_url : active.request.url };

	release( active );

	if ( !active.response.headers.empty() && spdlog::should_log( spdlog::level::trace ) )
		spdlog::trace(
			"downloader http: {} {} answered {} [{}]",
			httpMethodName( active.request.method ),
			detail::redactUrlQuery( final_url, active.request.sensitive_query ),
			status,
			describeHeaders( active.response.headers ) );

	TransferResult result { std::unexpected(
		TransferError { .code = TransferErrorCode::TRANSPORT, .message = "unknown transport failure" } ) };

	if ( !active.sink_error.empty() )
	{
		if ( active.request.buffered_sink ) (void)hostCallback( [ & ] { active.request.buffered_sink->abort(); } );

		result = std::unexpected( TransferError { .code = TransferErrorCode::SINK, .message = active.sink_error } );
	}
	else if ( active.over_limit )
	{
		result = std::unexpected(
			TransferError {
				.code = TransferErrorCode::TOO_LARGE,
				.message = format_ns::format(
					"Response body from {} exceeds the {} byte limit",
					detail::redactUrlQuery( active.request.url, active.request.sensitive_query ),
					active.request.options.max_response_bytes ) } );
	}
	// A deliberate header abort still delivers the HTTP 404 to the request's error handler.
	else if ( code != CURLE_OK && !active.stopped_on_not_found )
	{
		if ( active.request.buffered_sink ) (void)hostCallback( [ & ] { active.request.buffered_sink->abort(); } );

		const bool cancelled { active.request.cancellation && active.request.cancellation->load() };
		result = std::unexpected(
			TransferError { .code = cancelled ? TransferErrorCode::CANCELLED : TransferErrorCode::TRANSPORT,
		                    .message = active.error[ 0 ] == '\0' ? curl_easy_strerror( code ) :
		                                                           std::string { active.error.data() } } );
	}
	else
	{
		active.response.status = static_cast< std::int32_t >( status );
		active.response.url = std::move( final_url );

		const std::string_view content_type { active.response.headers.get( "content-type" ) };
		const bool wanted_a_file { active.request.buffered_sink != nullptr };
		const bool unexpected { !redirecting( active ) && ( wanted_a_file || active.response.status >= 400 ) };

		if ( unexpected && isMarkupContentType( content_type ) )
			spdlog::warn(
				"downloader http: {} answered {} with {}: {}",
				detail::redactUrlQuery( active.response.url, active.request.sensitive_query ),
				active.response.status,
				content_type,
				describeBody( wanted_a_file ? active.preview : active.response.body, active.response.bytes ) );

		if ( active.request.buffered_sink && preservingSink( active ) )
		{
			active.response.sink = std::move( active.request.buffered_sink );
			result = std::move( active.response );
		}
		else if (
			active.request.buffered_sink && active.request.import.options[ "requireFile" ].isBool()
		    && active.request.import.options[ "requireFile" ].asBool()
		    && !isDirectResponse( content_type, active.preview ) )
		{
			(void)hostCallback( [ & ] { active.request.buffered_sink->abort(); } );
			result = std::unexpected(
				TransferError {
					.code = TransferErrorCode::SINK,
					.message = "URL did not return a single file (HTML, structured data, or unknown content type)" } );
		}
		else if ( active.request.buffered_sink )
		{
			active.response.import_metadata = ImportMetadata {
				.size = active.response.bytes,
				.content_type = std::string { active.response.headers.get( "content-type" ) },
				.filename = filenameFor( active.response.url, active.response.headers ),
				.effective_url = active.response.url,
			};

			active.response.sink = std::move( active.request.buffered_sink );
			active.response.import_pending = true;
			active.response.import_finished = [ lane = m_lane.lock() ]
			{
				if ( lane ) lane->onTransferFinished();
			};
			result = std::move( active.response );
		}
		else
		{
			result = std::move( active.response );
		}
	}

	const bool deferred_import { result.has_value() && result.value().import_pending };
	if ( active.callback ) active.callback( std::move( result ) );

	m_assigned.fetch_sub( 1, std::memory_order_relaxed );
	const auto lane { m_lane.lock() };
	if ( lane )
	{
		if ( deferred_import )
			lane->onTransferFinalizing();
		else
			lane->onTransferFinished();
	}
}

void LaneShard::cancel( const std::shared_ptr< std::atomic_bool >& cancellation )
{
	for ( auto entry { m_active.begin() }; entry != m_active.end(); )
	{
		if ( entry->second->request.cancellation != cancellation )
		{
			++entry;
			continue;
		}

		auto node { m_active.extract( entry++ ) };
		Active& active { *node.mapped() };

		if ( active.request.buffered_sink ) (void)hostCallback( [ & ] { active.request.buffered_sink->abort(); } );

		release( active );

		if ( active.callback )
			active.callback(
				std::unexpected(
					TransferError { .code = TransferErrorCode::CANCELLED, .message = "Transfer cancelled" } ) );

		m_assigned.fetch_sub( 1, std::memory_order_relaxed );
		if ( const auto lane { m_lane.lock() } ) lane->onTransferFinished();
	}
}

} // namespace idhan::downloader
