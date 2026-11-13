#include <ada.h>
#include <expected>

#include "subscriptions.hpp"
#include "subscriptions/SubscriptionEventHub.hpp"

namespace idhan::httpapi
{
static std::expected< db::SubscriptionGroup, std::string > groupFromBody( SubscriptionGroupBody body )
{
	if ( body.name.empty() || body.name.size() > 200 )
		return std::unexpected( "name must contain 1 to 200 characters" );

	const auto separator { body.generator.rfind( ':' ) };
	if ( separator == std::string::npos || separator == 0 || separator + 1 == body.generator.size() )
		return std::unexpected( "generator must identify a parser and handler" );

	if ( !body.origin.empty() )
	{
		const auto origin_e { ada::parse< ada::url_aggregator >( body.origin ) };
		if ( !origin_e.has_value() ) return std::unexpected( "Invalid origin" );

		const auto& origin { origin_e.value() };
		if ( origin.get_protocol() != "https:" && origin.get_protocol() != "http:" )
			return std::unexpected( "Origin must use HTTP or HTTPS" );

		body.origin = origin.get_origin();
	}

	if ( body.interval_seconds < 60 || body.interval_seconds > 31536000 )
		return std::unexpected( "interval_seconds must be between 60 and 31536000" );

	return db::SubscriptionGroup {
		.name = std::move( body.name ),
		.generator = std::move( body.generator ),
		.interval_seconds = body.interval_seconds,
		.enabled = body.enabled,
		.origin = std::move( body.origin )
	};
}

template < int Status = 200 >
static http::HTTPTask< http::JsonResult< db::SubscriptionGroup, Status > > groupResponse(
	db::DBContext& db,
	const SubscriptionGroupID id )
{
	auto group { co_await db::findSubscriptionGroup( db, id ) };
	if ( !group.has_value() ) co_return http::Response::error( 404, "Group no longer exists" );

	co_await subscriptions::SubscriptionEventHub::instance().refresh( db, id );
	co_return http::JsonResponse< db::SubscriptionGroup, Status > { std::move( group.value() ) };
}

http::HTTPTask< http::JsonResult< std::vector< db::SubscriptionGroup > > > listSubscriptionGroups(
	db::DBContext& db,
	http::RequestContext )
{
	co_return http::JsonResponse< std::vector< db::SubscriptionGroup > > { co_await db::subscriptionGroups( db ) };
}

http::HTTPTask< http::JsonResult< db::SubscriptionGroup, 201 > > createSubscriptionGroup(
	db::DBContext& db,
	http::JsonBody< SubscriptionGroupBody > body )
{
	auto group_e { groupFromBody( std::move( body.value ) ) };
	if ( !group_e.has_value() ) co_return http::Response::error( 400, group_e.error() );

	const auto id { co_await db::createSubscriptionGroup( db, std::move( group_e.value() ) ) };
	co_return co_await groupResponse< 201 >( db, id );
}

http::HTTPTask< http::JsonResult< db::SubscriptionGroup > > updateSubscriptionGroup(
	db::DBContext& db,
	http::RequestContext context,
	http::JsonBody< SubscriptionGroupBody > body )
{
	const auto id_e { context.parameters().scalar< SubscriptionGroupID >( "group_id" ) };
	if ( !id_e.has_value() ) co_return http::Response::error( 400, id_e.error().message );

	auto group_e { groupFromBody( std::move( body.value ) ) };
	if ( !group_e.has_value() ) co_return http::Response::error( 400, group_e.error() );

	auto& group { group_e.value() };
	group.id = id_e.value();
	if ( !co_await db::updateSubscriptionGroup( db, std::move( group ) ) )
		co_return http::Response::error( 409, "Group is missing or its generator is running" );

	co_return co_await groupResponse( db, id_e.value() );
}

http::HTTPTask< http::JsonResult< DownloaderDeletedResponse > > deleteSubscriptionGroup(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto id_e { context.parameters().scalar< SubscriptionGroupID >( "group_id" ) };
	if ( !id_e.has_value() ) co_return http::Response::error( 400, id_e.error().message );

	if ( !co_await db::deleteSubscriptionGroup( db, id_e.value() ) )
		co_return http::Response::error( 409, "Group is missing or has running searches" );

	co_await subscriptions::SubscriptionEventHub::instance().refresh( db, id_e.value() );
	co_return http::JsonResponse< DownloaderDeletedResponse > {};
}

http::HTTPTask< http::JsonResult< db::SubscriptionGroup > > runSubscriptionGroup(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto id_e { context.parameters().scalar< SubscriptionGroupID >( "group_id" ) };
	if ( !id_e.has_value() ) co_return http::Response::error( 400, id_e.error().message );

	if ( !co_await db::requestSubscriptionGroupRun( db, id_e.value() ) )
		co_return http::Response::error( 409, "Group is missing or paused" );

	co_return co_await groupResponse( db, id_e.value() );
}
} // namespace idhan::httpapi
