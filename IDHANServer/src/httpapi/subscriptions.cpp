#include "subscriptions.hpp"

#include <algorithm>
#include <cctype>
#include <expected>

#include "maintenance/SubscriptionMaintenance.hpp"
#include "subscriptions/SubscriptionEventHub.hpp"

namespace idhan::httpapi
{
static std::expected< db::Subscription, std::string > subscriptionFromBody( SubscriptionBody body )
{
	if ( body.tags.size() > 8192 ) return std::unexpected( "tags must be at most 8192 characters" );

	if ( body.parameters.size() > 100 ) return std::unexpected( "Too many generator parameters" );

	for ( const auto& [ key, value ] : body.parameters )
	{
		if ( key.empty() || key.size() > 200 || value.size() > 8192 )
			return std::unexpected( "Invalid generator parameter size" );
	}
	if ( !body.tags.empty() ) body.parameters[ "tags" ] = body.tags;

	return db::Subscription {
		.tags = std::move( body.tags ), .enabled = body.enabled, .parameters = std::move( body.parameters )
	};
}

template < int Status = 200 >
static http::HTTPTask< http::JsonResult< db::Subscription, Status > > subscriptionResponse(
	db::DBContext& db,
	const SubscriptionID id )
{
	auto subscription { co_await db::findSubscription( db, id ) };
	if ( !subscription.has_value() ) co_return http::Response::error( 404, "Subscription no longer exists" );

	co_await subscriptions::SubscriptionEventHub::instance().refresh( db, subscription.value().group_id );
	co_return http::JsonResponse< db::Subscription, Status > { std::move( subscription.value() ) };
}

http::HTTPTask< http::JsonResult< std::vector< db::Subscription > > > listSubscriptions(
	db::DBContext& db,
	http::RequestContext )
{
	co_return http::JsonResponse< std::vector< db::Subscription > > { co_await db::subscriptions( db ) };
}

http::HTTPTask< http::JsonResult< db::Subscription, 201 > > createSubscription(
	db::DBContext& db,
	http::RequestContext context,
	http::JsonBody< SubscriptionBody > body )
{
	const auto group_id_e { context.parameters().scalar< SubscriptionGroupID >( "group_id" ) };
	if ( !group_id_e.has_value() ) co_return http::Response::error( 400, group_id_e.error().message );

	auto subscription_e { subscriptionFromBody( std::move( body.value ) ) };
	if ( !subscription_e.has_value() ) co_return http::Response::error( 400, subscription_e.error() );

	subscription_e.value().group_id = group_id_e.value();
	const auto id { co_await db::createSubscription( db, std::move( subscription_e.value() ) ) };
	if ( !id.has_value() ) co_return http::Response::error( 409, "Group is missing or this search already exists" );

	co_return co_await subscriptionResponse< 201 >( db, id.value() );
}

http::HTTPTask< http::JsonResult< db::Subscription > > updateSubscription(
	db::DBContext& db,
	http::RequestContext context,
	http::JsonBody< SubscriptionBody > body )
{
	const auto id_e { context.parameters().scalar< SubscriptionID >( "subscription_id" ) };
	if ( !id_e.has_value() ) co_return http::Response::error( 400, id_e.error().message );

	auto subscription_e { subscriptionFromBody( std::move( body.value ) ) };
	if ( !subscription_e.has_value() ) co_return http::Response::error( 400, subscription_e.error() );

	auto& subscription { subscription_e.value() };
	subscription.id = id_e.value();
	if ( !co_await db::updateSubscription( db, std::move( subscription ) ) )
		co_return http::Response::error( 409, "Subscription is missing or its search is running" );

	co_return co_await subscriptionResponse( db, id_e.value() );
}

http::HTTPTask< http::JsonResult< db::Subscription > > runSubscription(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto id_e { context.parameters().scalar< SubscriptionID >( "subscription_id" ) };
	if ( !id_e.has_value() ) co_return http::Response::error( 400, id_e.error().message );

	if ( !co_await db::requestSubscriptionRun( db, id_e.value() ) )
		co_return http::Response::error( 409, "Subscription is missing, paused, or already running" );

	maintenance::SubscriptionMaintenance::wake();

	co_return co_await subscriptionResponse( db, id_e.value() );
}

http::HTTPTask< http::JsonResult< DownloaderDeletedResponse > > deleteSubscription(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto id_e { context.parameters().scalar< SubscriptionID >( "subscription_id" ) };
	if ( !id_e.has_value() ) co_return http::Response::error( 400, id_e.error().message );

	const auto previous { co_await db::findSubscription( db, id_e.value() ) };
	if ( !co_await db::deleteSubscription( db, id_e.value() ) )
		co_return http::Response::error( 409, "Subscription is missing or still running" );

	if ( previous.has_value() )
		co_await subscriptions::SubscriptionEventHub::instance().refresh( db, previous.value().group_id );

	co_return http::JsonResponse< DownloaderDeletedResponse > {};
}

http::HTTPTask< http::JsonResult< SubscriptionBatchResult > > createSubscriptionBatch(
	db::DBContext& db,
	http::RequestContext context,
	http::JsonBody< SubscriptionBatchBody > body )
{
	const auto id_e { context.parameters().scalar< SubscriptionGroupID >( "group_id" ) };
	if ( !id_e.has_value() ) co_return http::Response::error( 400, id_e.error().message );

	if ( body.value.items.empty() || body.value.items.size() > 5000 )
		co_return http::Response::error( 400, "Supply between 1 and 5000 queries" );

	if ( !( co_await db::findSubscriptionGroup( db, id_e.value() ) ).has_value() )
		co_return http::Response::error( 404, "Group no longer exists" );

	std::vector< db::Subscription > items;
	for ( auto& item : body.value.items )
	{
		auto item_e { subscriptionFromBody( std::move( item ) ) };
		if ( !item_e.has_value() ) co_return http::Response::error( 400, item_e.error() );

		items.push_back( std::move( item_e.value() ) );
	}
	auto ids { co_await db::createSubscriptions( db, id_e.value(), std::move( items ) ) };
	co_await subscriptions::SubscriptionEventHub::instance().refresh( db, id_e.value() );
	const auto added { static_cast< std::int64_t >( ids.size() ) };
	co_return http::JsonResponse< SubscriptionBatchResult > {
		{ std::move( ids ), added, static_cast< std::int64_t >( body.value.items.size() ) - added }
	};
}

http::HTTPTask< http::JsonResult< SubscriptionRecordsResponse > > subscriptionRecords(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto id_e { context.parameters().scalar< SubscriptionID >( "subscription_id" ) };
	if ( !id_e.has_value() ) co_return http::Response::error( 400, id_e.error().message );

	if ( !( co_await db::findSubscription( db, id_e.value() ) ).has_value() )
		co_return http::Response::error( 404, "Subscription no longer exists" );

	co_return http::JsonResponse< SubscriptionRecordsResponse > {
		{ co_await db::subscriptionRunRecords( db, id_e.value() ) }
	};
}
} // namespace idhan::httpapi
