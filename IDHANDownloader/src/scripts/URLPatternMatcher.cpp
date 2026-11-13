#include "scripts/URLPatternMatcher.hpp"

#include <ada.h>
#include <array>
#include <atomic>
#include <chrono>
#include <map>

#include "js/QuickJSHandles.hpp"
#include "js/QuickJSRegexProvider.hpp"
#include "js/bindings/IdhanBindings.hpp"

namespace idhan::downloader
{
int interruptURLPattern( JSRuntime*, void* opaque )
{
	const auto* deadline { static_cast< std::chrono::steady_clock::time_point* >( opaque ) };
	return std::chrono::steady_clock::now() >= *deadline;
}

class URLPatternThreadCache
{
	JSRuntimePtr m_runtime { JS_NewRuntime() };
	JSContextPtr m_context {};
	std::chrono::steady_clock::time_point m_deadline {};
	// Bounded so old registry generations cannot accumulate on long-lived threads.
	std::map< std::uint64_t, ada::url_pattern< quickjs::QuickJSRegexProvider > > m_patterns {};

  public:

	URLPatternThreadCache()
	{
		if ( m_runtime == nullptr ) return;

		JS_SetMemoryLimit( m_runtime.get(), 32 * 1024 * 1024 );
		JS_SetMaxStackSize( m_runtime.get(), 1024 * 1024 );
		JS_SetInterruptHandler( m_runtime.get(), interruptURLPattern, &m_deadline );
		m_context.reset( JS_NewContext( m_runtime.get() ) );
	}

	[[nodiscard]] JSContext* context() const { return m_context.get(); }

	[[nodiscard]] std::expected< ada::url_pattern< quickjs::QuickJSRegexProvider >*, std::string > prepare(
		const std::uint64_t id,
		const ada::url_pattern_init& init )
	{
		if ( m_context == nullptr ) return std::unexpected( "Unable to create URLPattern runtime" );

		JS_UpdateStackTop( m_runtime.get() );
		m_deadline = std::chrono::steady_clock::now() + std::chrono::seconds { 5 };
		const auto found { m_patterns.find( id ) };
		if ( found != m_patterns.end() ) return &found->second;

		quickjs::QuickJSRegexProvider::Scope scope { m_context.get() };
		auto pattern_e { ada::parse_url_pattern< quickjs::QuickJSRegexProvider >( init ) };
		if ( JS_HasException( m_context.get() ) ) return std::unexpected( scriptExceptionString( m_context.get() ) );

		if ( !pattern_e.has_value() ) return std::unexpected( "Invalid URLPattern" );

		if ( m_patterns.size() >= 256 ) m_patterns.clear();

		const auto inserted { m_patterns.emplace( id, std::move( pattern_e.value() ) ) };
		return &inserted.first->second;
	}
};

URLPatternThreadCache& urlPatternThreadCache()
{
	thread_local URLPatternThreadCache cache;
	return cache;
}

class URLPatternMatcher::Impl
{
  public:

	std::uint64_t id {};
	ada::url_pattern_init init {};
	std::vector< std::pair< std::string, std::string > > query {};
};

URLPatternMatcher::URLPatternMatcher( std::unique_ptr< Impl > impl ) : m_impl( std::move( impl ) )
{}

URLPatternMatcher::~URLPatternMatcher() = default;

std::expected< std::shared_ptr< URLPatternMatcher >, std::string > URLPatternMatcher::create(
	const Json::Value& definition )
{
	static std::atomic_uint64_t next_id {};
	auto impl { std::make_unique< Impl >() };
	impl->id = next_id.fetch_add( 1 );

	struct Field
	{
		const char* name;
		std::optional< std::string > ada::url_pattern_init::* member;
	};
	constexpr std::array fields {
		Field { "baseURL", &ada::url_pattern_init::base_url },  Field { "protocol", &ada::url_pattern_init::protocol },
		Field { "username", &ada::url_pattern_init::username }, Field { "password", &ada::url_pattern_init::password },
		Field { "hostname", &ada::url_pattern_init::hostname }, Field { "port", &ada::url_pattern_init::port },
		Field { "pathname", &ada::url_pattern_init::pathname }, Field { "search", &ada::url_pattern_init::search },
		Field { "hash", &ada::url_pattern_init::hash }
	};
	for ( const auto& field : fields )
	{
		if ( !definition.isMember( field.name ) ) continue;

		if ( !definition[ field.name ].isString() ) return std::unexpected( "URLPattern components must be strings" );

		impl->init.*( field.member ) = definition[ field.name ].asString();
	}
	if ( definition.isMember( "query" ) )
	{
		const auto& query { definition[ "query" ] };
		if ( !query.isObject() ) return std::unexpected( "URLPattern query must be an object" );

		for ( const auto& key : query.getMemberNames() )
		{
			if ( !query[ key ].isString() ) return std::unexpected( "Query patterns must be strings" );

			impl->query.emplace_back( key, query[ key ].asString() );
		}
	}
	const auto prepared_e { urlPatternThreadCache().prepare( impl->id, impl->init ) };
	if ( !prepared_e.has_value() ) return std::unexpected( prepared_e.error() );

	return std::shared_ptr< URLPatternMatcher > { new URLPatternMatcher { std::move( impl ) } };
}

std::expected< std::optional< Json::Value >, std::string > URLPatternMatcher::match(
	const std::string_view url,
	ada::url_search_params& parameters ) const
{
	auto& cache { urlPatternThreadCache() };
	const auto prepared_e { cache.prepare( m_impl->id, m_impl->init ) };
	if ( !prepared_e.has_value() ) return std::unexpected( prepared_e.error() );

	JSContext* context { cache.context() };
	quickjs::QuickJSRegexProvider::Scope scope { context };
	auto result_e { prepared_e.value()->exec( url ) };
	if ( JS_HasException( context ) ) return std::unexpected( scriptExceptionString( context ) );

	if ( !result_e.has_value() ) return std::unexpected( "Unable to match URLPattern" );

	const auto& result { result_e.value() };
	if ( !result.has_value() ) return std::optional< Json::Value > {};

	Json::Value captures { Json::objectValue };
	const std::array components { &result->protocol, &result->username, &result->password, &result->hostname,
	                              &result->port,     &result->pathname, &result->search,   &result->hash };

	for ( const auto* component : components )
	{
		for ( const auto& [ name, value ] : component->groups )
		{
			if ( !value.has_value() || name.empty() || ( name[ 0 ] >= '0' && name[ 0 ] <= '9' ) ) continue;

			const auto percent { value->find( '%' ) };
			const auto decoded {
				percent == std::string::npos ? value.value() : ada::unicode::percent_decode( value.value(), percent )
			};

			if ( captures.isMember( name ) && captures[ name ].asString() != decoded )
				return std::optional< Json::Value > {};

			captures[ name ] = decoded;
		}
	}

	for ( const auto& [ key, pattern ] : m_impl->query )
	{
		const auto values { parameters.get_all( key ) };
		if ( values.size() != 1 ) return std::optional< Json::Value > {};

		if ( pattern.starts_with( ':' ) && pattern.size() > 1 )
		{
			const auto name { pattern.substr( 1 ) };
			if ( captures.isMember( name ) && captures[ name ].asString() != values[ 0 ] )
				return std::optional< Json::Value > {};

			captures[ name ] = values[ 0 ];
		}
		else if ( pattern != values[ 0 ] )
		{
			return std::optional< Json::Value > {};
		}
	}
	return std::optional< Json::Value > { std::move( captures ) };
}

std::expected< std::optional< Json::Value >, std::string > matchURLPattern(
	const Json::Value& definition,
	const std::string_view url )
{
	const auto matcher_e { URLPatternMatcher::create( definition ) };
	if ( !matcher_e.has_value() ) return std::unexpected( matcher_e.error() );

	const auto parsed_e { ada::parse< ada::url_aggregator >( url ) };
	if ( !parsed_e.has_value() ) return std::unexpected( "Invalid URL" );

	ada::url_search_params parameters { parsed_e.value().get_search() };
	return matcher_e.value()->match( url, parameters );
}
} // namespace idhan::downloader
