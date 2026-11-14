#include "subscriptions.hpp"

#include <json/reader.h>
#include <json/writer.h>

#include <sstream>
#include <stdexcept>

#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBTransaction.hpp"

namespace idhan::db
{
static std::string subscriptionValues( const Subscription& subscription )
{
	Json::Value values{ Json::objectValue };
	for ( const auto& [ key, value ] : subscription.parameters )
		values[ key ] = value;
	if ( !subscription.tags.empty() )
		values[ "tags" ] = subscription.tags;

	return Json::writeString( Json::StreamWriterBuilder{}, values );
}

static std::map< std::string, std::string > subscriptionParameters( const std::string& text )
{
	Json::Value values;
	Json::CharReaderBuilder reader;
	std::string error;
	std::istringstream input{ text };
	if ( !Json::parseFromStream( reader, input, &values, &error ) || !values.isObject() )
		throw std::runtime_error( "Invalid subscription parameters" );

	std::map< std::string, std::string > parameters;
	for ( const auto& key : values.getMemberNames() )
	{
		if ( !values[ key ].isString() )
			continue;

		parameters.emplace( key, values[ key ].asString() );
	}
	return parameters;
}

static runtime::Task< std::vector< Subscription > > loadSubscriptions(
	DBContext& db,
	const SubscriptionID filter_id,
	const SubscriptionGroupID group_id = 0 )
{
	DBBindings bindings;
	bindings.bind( filter_id );
	bindings.bind( group_id );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
			SELECT subscription_id, subscription_group_id, enabled,
			       extract(epoch FROM next_run_at)::bigint, active_session_id,
			       last_session_id, extract(epoch FROM last_finished_at)::bigint,
			       last_error, values::text, session_id, run_requested
			FROM subscriptions
			WHERE ($1::integer = 0 OR subscription_id = $1)
			  AND ($2::integer = 0 OR subscription_group_id = $2)
			ORDER BY subscription_id
		)SQL",
				std::move( bindings ) )
			.rows<
				SubscriptionID,
				SubscriptionGroupID,
				bool,
				std::int64_t,
				std::optional< DownloadSessionID >,
				std::optional< DownloadSessionID >,
				std::optional< std::int64_t >,
				std::optional< std::string >,
				std::string,
				std::optional< DownloadSessionID >,
				bool >()
	};
	std::vector< Subscription > result;
	for ( const auto& [ id, group, enabled, next, active, last, finished, error, values, session, requested ] : rows )
	{
		auto parameters { subscriptionParameters( values ) };
		const auto tags { parameters.find( "tags" ) };
		result.push_back(
			{ .id = id,
			  .group_id = group,
			  .tags = tags == parameters.end() ? std::string {} : tags->second,
			  .enabled = enabled,
			  .next_run_at = next,
			  .active_session_id = active,
			  .last_session_id = last,
			  .last_finished_at = finished,
			  .last_error = error,
			  .parameters = std::move( parameters ),
			  .session_id = session,
			  .run_requested = requested } );
	}
	co_return result;
}

runtime::Task< std::vector< Subscription > > subscriptionsInGroup( DBContext& db, const SubscriptionGroupID group_id )
{
	co_return co_await loadSubscriptions( db, 0, group_id );
}

runtime::Task< std::vector< Subscription > > subscriptions( DBContext& db )
{
	co_return co_await loadSubscriptions( db, 0 );
}

runtime::Task< std::optional< Subscription > > findSubscription( DBContext& db, const SubscriptionID id )
{
	auto rows{ co_await loadSubscriptions( db, id ) };
	if ( rows.empty() )
		co_return std::nullopt;

	co_return std::move( rows.front() );
}

runtime::Task< std::optional< SubscriptionID > > createSubscription( DBContext& db, Subscription subscription )
{
	DBBindings bindings;
	bindings.bind( subscription.group_id );
	bindings.bind( subscriptionValues( subscription ) );
	bindings.bind( subscription.enabled );
	const auto rows{
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
						 INSERT INTO subscriptions (subscription_group_id, values, enabled)
						 SELECT
						     subscription_group_id,
						     $2::json,
						     $3
						 FROM subscription_groups
						 WHERE subscription_group_id = $1
						 ON CONFLICT
						     DO NOTHING
						 RETURNING subscription_id
					 )SQL",
					 std::move( bindings ) )
				 .rows< SubscriptionID >()
	};
	if ( rows.empty() )
		co_return std::nullopt;

	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< bool > updateSubscription( DBContext& db, Subscription subscription )
{
	DBBindings bindings;
	bindings.bind( subscriptionValues( subscription ) );
	bindings.bind( subscription.enabled );
	bindings.bind( subscription.id );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
						 UPDATE
						     subscriptions
						 SET enabled = $2,
						     run_requested = run_requested
						     AND $2,
						     next_run_at = CASE WHEN values::jsonb <> $1::jsonb THEN now()
						     ELSE next_run_at END,
						     values = $1::json
						 WHERE subscription_id = $3
						     AND (active_session_id IS NULL
						         OR values::jsonb = $1::jsonb)
						     AND NOT EXISTS (
						         SELECT
						             1
						         FROM subscriptions other
						         WHERE other.subscription_group_id = subscriptions.subscription_group_id
						             AND other.subscription_id <> $3
						             AND other.values::jsonb = $1::jsonb
						     )
						 RETURNING subscription_id
					 )SQL",
				std::move( bindings ) )
			.rows< SubscriptionID >()
	};
	co_return !rows.empty();
}

runtime::Task< bool > requestSubscriptionRun( DBContext& db, const SubscriptionID id )
{
	DBBindings bindings;
	bindings.bind( id );
	const auto rows{
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
						 UPDATE
						     subscriptions
						 SET next_run_at = now(),
						     run_requested = true
						 WHERE subscription_id = $1
						     AND enabled
						     AND active_session_id IS NULL
						     AND EXISTS (
						         SELECT
						             1
						         FROM subscription_groups g
						         WHERE g.subscription_group_id = subscriptions.subscription_group_id
						             AND g.enabled
						     )
						 RETURNING subscription_id
					 )SQL",
					 std::move( bindings ) )
				 .rows< SubscriptionID >()
	};
	co_return !rows.empty();
}

runtime::Task< bool > deleteSubscription( DBContext& db, const SubscriptionID id )
{
	DBBindings bindings;
	bindings.bind( id );
	const auto rows{
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
						 DELETE FROM subscriptions
						 WHERE subscription_id = $1
						     AND active_session_id IS NULL
						 RETURNING subscription_id
					 )SQL",
					 std::move( bindings ) )
				 .rows< SubscriptionID >()
	};
	co_return !rows.empty();
}

runtime::Task<> queueInterruptedSubscriptions( DBContext& db )
{
	// A fresh check is safe to repeat. Download history remains owned by the downloader.
	co_await db
		.query(
			//language=postgresql
			R"SQL(
			UPDATE subscriptions
			SET last_session_id = active_session_id, active_session_id = NULL,
			    last_finished_at = now(), last_error = 'Interrupted by server restart',
			    next_run_at = now(), run_requested = false
			WHERE active_session_id IS NOT NULL
			  AND NOT EXISTS (SELECT 1 FROM download_session_results r
			                  WHERE r.download_session_id = subscriptions.active_session_id)
		)SQL" )
		.result();
}

runtime::Task< std::optional< ScheduledSubscription > > nextScheduledSubscription(
	DBTransaction& transaction,
	const SubscriptionGroupID group_id )
{
	db::DBBindings bindings;
	bindings.bind( group_id );
	const auto rows {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
			SELECT s.subscription_id, s.values::text, g.generator, g.origin, d.download_session_id
			FROM subscriptions s JOIN subscription_groups g USING (subscription_group_id)
			LEFT JOIN download_sessions d ON d.download_session_id = s.session_id
			WHERE g.subscription_group_id = $1 AND s.active_session_id IS NULL AND s.enabled AND g.enabled
			  AND s.next_run_at <= now()
			  AND NOT EXISTS (SELECT 1 FROM subscriptions running
			                  WHERE running.subscription_group_id = g.subscription_group_id
			                    AND running.active_session_id IS NOT NULL)
			ORDER BY s.run_requested DESC, s.next_run_at, s.subscription_id
			LIMIT 1 FOR UPDATE OF g, s SKIP LOCKED
		)SQL",
				std::move( bindings ) )
			.rows< SubscriptionID, std::string, std::string, std::string, std::optional< DownloadSessionID > >()
	};
	if ( rows.empty() ) co_return std::nullopt;

	const auto& [ id, values, generator, origin, session ] { rows.row( 0 ) };
	co_return ScheduledSubscription { id, subscriptionParameters( values ), generator, origin, session };
}

runtime::Task<> startSubscription(
	DBTransaction& transaction,
	const SubscriptionID id,
	const DownloadSessionID session_id )
{
	DBBindings bindings;
	bindings.bind( id );
	bindings.bind( session_id );
	co_await transaction
		.query(
			//language=postgresql
			R"SQL(
			UPDATE subscriptions
			SET active_session_id = $2, session_id = $2, run_requested = false
			WHERE subscription_id = $1
		)SQL",
			std::move( bindings ) )
		.result();
}

runtime::Task< bool > finishSubscription(
	DBContext& db,
	const SubscriptionID id,
	const DownloadSessionID session_id,
	std::optional< std::string > error )
{
	DBBindings bindings;
	bindings.bind( id );
	bindings.bind( session_id );
	bindings.bind( std::move( error ) );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
			UPDATE subscriptions s
			SET active_session_id = NULL, last_session_id = $2,
			    last_finished_at = now(), last_error = $3,
			    next_run_at = now() + g.interval_seconds * interval '1 second'
			FROM subscription_groups g
			WHERE s.subscription_id = $1 AND s.active_session_id = $2
			  AND g.subscription_group_id = s.subscription_group_id
			RETURNING s.subscription_id
		)SQL",
				std::move( bindings ) )
			.rows< SubscriptionID >()
	};
	co_return !rows.empty();
}

runtime::Task< std::vector< SubscriptionID > > createSubscriptions(
	DBContext& db,
	const SubscriptionGroupID group_id,
	std::vector< Subscription > items )
{
	Json::Value values{ Json::arrayValue };
	for ( const auto& item : items )
	{
		Json::Value value;
		value[ "parameters" ] = subscriptionValues( item );
		value[ "enabled" ] = item.enabled;
		values.append( std::move( value ) );
	}
	DBBindings bindings;
	bindings.bind( group_id );
	bindings.bind( Json::writeString( Json::StreamWriterBuilder{}, values ) );
	const auto rows{
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
						 INSERT INTO subscriptions (subscription_group_id, values, enabled)
						 SELECT
						     g.subscription_group_id,
						     (item ->> 'parameters')::json,
						     (item ->> 'enabled')::boolean
						 FROM subscription_groups g
						     CROSS JOIN jsonb_array_elements($2::jsonb) item
						 WHERE g.subscription_group_id = $1
						 ON CONFLICT
						     DO NOTHING
						 RETURNING subscription_id
					 )SQL",
					 std::move( bindings ) )
				 .rows< SubscriptionID >()
	};
	std::vector< SubscriptionID > result;
	for ( const auto& [ id ] : rows )
		result.push_back( id );
	co_return result;
}
} // namespace idhan::db
