#include "events.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "apiAuth.hpp"
#include "downloader/DownloadSessionEventHub.hpp"
#include "events/ServerEvents.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/streaming/SSEBodySource.hpp"
#include "jobs/JobContext.hpp"
#include "subscriptions/SubscriptionEventHub.hpp"

namespace idhan::httpapi
{
static std::vector< std::string > splitTypes( const std::string_view text )
{
	std::vector< std::string > types {};
	std::size_t begin { 0 };
	while ( begin < text.size() )
	{
		const auto end { text.find( ',', begin ) };
		const auto length { end == std::string_view::npos ? text.size() - begin : end - begin };
		if ( length > 0 ) types.emplace_back( text.substr( begin, length ) );
		if ( end == std::string_view::npos ) break;
		begin = end + 1;
	}

	return types;
}

static constexpr std::array< std::string_view, 3 > known_streams { { "downloader", "job", "subscriptions" } };

static bool includesStream( const std::vector< std::string >& streams, const std::string_view stream )
{
	if ( streams.empty() ) return true;

	return std::ranges::find( streams, stream ) != streams.end();
}

http::HTTPTask< http::EncodedResult< std::string, "text/event-stream" > > events(
	db::DBContext& db,
	http::Query< EventsQuery > query )
{
	const auto streams {
		query.value.filter.has_value() ? splitTypes( query.value.filter.value() ) : std::vector< std::string > {}
	};
	for ( const auto& stream : streams )
	{
		if ( std::ranges::find( known_streams, stream ) != known_streams.end() ) continue;

		co_return http::Response::error( 400, std::format( "Unknown event stream '{}'", stream ) );
	}

	std::vector< DownloadSessionID > ids;
	std::vector< std::string > entities;
	if ( query.value.session_ids.has_value() )
	{
		const auto& text { query.value.session_ids.value() };
		for ( const auto& field : splitTypes( text ) )
		{
			DownloadSessionID id {};
			const auto parsed { std::from_chars( field.data(), field.data() + field.size(), id ) };
			if ( parsed.ec != std::errc {} || parsed.ptr != field.data() + field.size() || id <= 0 )
				co_return http::Response::error( 400, "session_ids must contain positive integers" );

			ids.push_back( id );
			entities.push_back( std::to_string( id ) );
		}

		const bool malformed { text.starts_with( ',' ) || text.ends_with( ',' ) || text.contains( ",," ) };
		if ( ids.empty() || ids.size() > 1000 || malformed )
			co_return http::Response::error( 400, "session_ids must contain 1 to 1000 positive integers" );
	}

	// Subscribe before reading any baseline so changes made while it is read stay queued behind it.
	auto subscription { server::events().subscribeEvents( streams, {}, false, std::move( entities ) ) };
	if ( includesStream( streams, "job" ) )
	{
		std::vector< http::SSEInitialEvent > jobs;
		jobs.push_back( getJobRuntime().eventSnapshot() );
		subscription.prependSnapshots( std::move( jobs ), subscription.checkpoint() );
	}

	if ( includesStream( streams, "downloader" ) )
		co_await downloader::DownloadSessionEventHub::instance().initializeSubscription(
			db, subscription, std::move( ids ) );

	if ( includesStream( streams, "subscriptions" ) )
		co_await subscriptions::SubscriptionEventHub::instance().initializeSubscription( db, subscription );

	http::Response response {};
	response.body = proxygen::coro::HTTPSourceHolder { new http::SSEBodySource {
		std::move( response.message ), std::move( subscription ) } };
	co_return http::encodedResponse< std::string, "text/event-stream" >( std::move( response ) );
}

} // namespace idhan::httpapi
