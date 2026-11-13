#include "subscriptions.hpp"

#include <json/reader.h>
#include <json/writer.h>

#include <ada.h>
#include <sstream>
#include <stdexcept>

#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBTransaction.hpp"
#include "subscriptionRecordQueries.hpp"

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
				std::string { subscriptionRecordsCte } +
				//language=postgresql
				R"SQL(
						SELECT
						    subscription_id,
						    subscription_group_id,
						    COALESCE(values ->> 'tags', ''),
						    enabled,
						    extract(epoch FROM next_run_at)::bigint,
						    initial_sync_complete,
						    active_session_id,
						    last_session_id,
						    extract(epoch FROM last_finished_at)::bigint,
						    last_error,
						    extract(epoch FROM latest_post_at)::bigint,
						    post_dates_complete,
						    dead,
						    (
						        SELECT
						            count(*)
						        FROM subscription_records log
						        WHERE log.subscription_id = subscriptions.subscription_id
						    ),
						    values::text,
						    session_id,
						    active_root_id,
						    run.imported_count,
						    run.known_count,
						    (
						        SELECT
						            COALESCE(sum(f.size), 0)::bigint
						        FROM file_info f
						        WHERE EXISTS (
						                SELECT
						                    1
						                FROM subscription_records log
						                WHERE log.subscription_id = subscriptions.subscription_id
						                    AND log.record_id = f.record_id
						        )
						    )
						FROM subscriptions
						    LEFT JOIN LATERAL ( WITH RECURSIVE run_urls AS (
						            SELECT
						                u.download_session_url_id,
						                u.record_id,
						                u.state
						            FROM download_session_urls u
						            WHERE u.download_session_id = subscriptions.session_id
						                AND u.download_session_url_id = (
						                    SELECT
						                        max(r.download_session_url_id)
						                    FROM download_session_urls r
						                    WHERE r.download_session_id = subscriptions.session_id
						                        AND r.parent_url_id IS NULL
						                        AND r.download_session_url_id IS DISTINCT FROM subscriptions.active_root_id
						                )
						            UNION ALL
						            SELECT
						                c.download_session_url_id,
						                c.record_id,
						                c.state
						            FROM download_session_urls c
						                JOIN run_urls p ON c.parent_url_id = p.download_session_url_id
						            WHERE c.download_session_id = subscriptions.session_id
						    )
						        SELECT
						            count(DISTINCT record_id) FILTER (WHERE state = 'imported') AS imported_count,
						            count(DISTINCT record_id) - count(DISTINCT record_id) FILTER (WHERE state = 'imported') AS known_count
						        FROM run_urls) run ON true
						WHERE ($1::integer = 0
						    OR subscription_id = $1)
						AND ($2::integer = 0
						    OR subscription_group_id = $2)
						ORDER BY
						    subscription_id
					)SQL",
				std::move( bindings ) )
			.rows<
				SubscriptionID,
				SubscriptionGroupID,
				std::string,
				bool,
				std::int64_t,
				bool,
				std::optional< DownloadSessionID >,
				std::optional< DownloadSessionID >,
				std::optional< std::int64_t >,
				std::optional< std::string >,
				std::optional< std::int64_t >,
				bool,
				bool,
				std::int64_t,
				std::string,
				std::optional< DownloadSessionID >,
				std::optional< DownloadSessionUrlID >,
				std::int64_t,
				std::int64_t,
				std::int64_t >()
	};
	std::vector< Subscription > result;
	for (
		const auto& [ id, group_id, tags, enabled, next, initial, active, last, finished, error, latest, complete, dead,
			records, parameters, session_id, active_root_id, imported, known, total_size_bytes ] :
		rows )
		result.push_back( {
			id,
			group_id,
			tags,
			enabled,
			next,
			initial,
			active,
			last,
			finished,
			error,
			latest,
			complete,
			dead,
			records,
			subscriptionParameters( parameters ),
			session_id,
			active_root_id,
			imported,
			known,
			total_size_bytes
		} );
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
	const auto rows{
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
						 UPDATE
						     subscriptions
						 SET enabled = $2,
						     run_requested = run_requested
						     AND $2,
						     initial_sync_complete = CASE WHEN values::jsonb <> $1::jsonb THEN false
						     ELSE
						         initial_sync_complete
						     END,
						     next_run_at = CASE WHEN values::jsonb <> $1::jsonb THEN now()
						     ELSE
						         next_run_at
						     END,
						     latest_post_at = CASE WHEN values::jsonb <> $1::jsonb THEN NULL
						     ELSE
						         latest_post_at
						     END,
						     post_dates_complete = CASE WHEN values::jsonb <> $1::jsonb THEN false
						     ELSE
						         post_dates_complete
						     END,
						     dead = CASE WHEN values::jsonb <> $1::jsonb THEN false
						     ELSE
						         dead
						     END,
						     values = $1::json
						 WHERE subscription_id = $3
						     AND (active_root_id IS NULL
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

runtime::Task< > queueInterruptedSubscriptions( DBContext& db )
{
	co_await db
			 .query(
				 //language=postgresql
				 R"SQL(
					 UPDATE
					     subscriptions
					 SET active_session_id = NULL
					 WHERE active_session_id IS NOT NULL
					     AND active_root_id IS NOT NULL
					     AND EXISTS (
					         SELECT
					             1
					         FROM download_session_urls u
					         WHERE u.download_session_id = subscriptions.active_session_id
					             AND u.state IN ('pending', 'processing')
					     )
				 )SQL" )
			 .result();
}

runtime::Task< std::optional< SubscriptionRun > > claimSubscription( DBContext& db )
{
	auto transaction{ co_await db.transaction() };
	DBBindings due_bindings;
	due_bindings.bind( max_running_per_group );
	const auto due{
		co_await transaction
				 .query(
					 //language=postgresql
					 R"SQL(
						 SELECT
						     subscription_id,
						     values::text,
						     groups.generator,
						     groups.origin,
						     subscriptions.session_id,
						     active_root_id
						 FROM subscriptions
						     JOIN subscription_groups groups USING (subscription_group_id)
						 WHERE active_session_id IS NULL
						     AND subscriptions.enabled
						     AND groups.enabled
						     AND (run_requested
						         OR ((active_root_id IS NOT NULL
						                 OR next_run_at <= now())
						             AND (
						                 SELECT
						                     count(*)
						                 FROM subscriptions running
						                 WHERE running.subscription_group_id = subscriptions.subscription_group_id
						                     AND running.active_session_id IS NOT NULL
						             ) < $1))
						 ORDER BY
						     run_requested DESC,
						     (active_root_id IS NULL),
						     next_run_at,
						     subscription_id
						 LIMIT 1
						 FOR UPDATE
						     OF subscriptions,
						     groups SKIP LOCKED
					 )SQL",
					 std::move( due_bindings ) )
				 .rows<
					 SubscriptionID,
					 std::string,
					 std::string,
					 std::string,
					 std::optional< DownloadSessionID >,
					 std::optional< DownloadSessionUrlID > >()
	};
	if ( due.empty() )
		co_return std::nullopt;

	const auto& [ id, values, generator, origin, permanent_session, interrupted_root ]{ due.row( 0 ) };
	if ( interrupted_root.has_value() )
	{
		if ( !permanent_session.has_value() )
			throw std::runtime_error( "Interrupted subscription has no saved session" );

		DBBindings saved_bindings;
		saved_bindings.bind( permanent_session.value() );
		saved_bindings.bind( interrupted_root.value() );
		const auto saved{
			co_await transaction
					 .query(
						 //language=postgresql
						 R"SQL(
							 SELECT
							     u.url,
							     s.options::text
							 FROM download_session_urls u
							     JOIN download_sessions s USING (download_session_id)
							 WHERE u.download_session_id = $1
							     AND u.download_session_url_id = $2
						 )SQL",
						 std::move( saved_bindings ) )
					 .rows< std::string, std::string >()
		};
		if ( saved.empty() )
			throw std::runtime_error( "Interrupted subscription root no longer exists" );

		const auto& [ url, settings ]{ saved.row( 0 ) };
		DBBindings bindings;
		bindings.bind( id );
		co_await transaction
				 .query(
					 //language=postgresql
					 R"SQL(
						 UPDATE
						     subscriptions
						 SET active_session_id = session_id,
						     run_requested = false
						 WHERE subscription_id = $1
					 )SQL",
					 std::move( bindings ) )
				 .result();
		SubscriptionRun run{ id, permanent_session.value(), url, settings, interrupted_root.value(), true };
		co_await transaction.commit();
		co_return run;
	}

	Json::Value settings{ Json::objectValue };
	const auto separator{ generator.rfind( ':' ) };
	const auto parser{ generator.substr( 0, separator ) };
	settings[ "required_parser" ] = parser;
	settings[ "generator" ][ "parser" ] = parser;
	settings[ "generator" ][ "handler" ] = generator.substr( separator + 1 );
	settings[ "generator" ][ "origin" ] = origin;
	settings[ "generator" ][ "parameters" ] = Json::Value{ Json::objectValue };
	for ( const auto& [ key, value ] : subscriptionParameters( values ) )
		settings[ "generator" ][ "parameters" ][ key ] = value;
	const auto settings_json{ Json::writeString( Json::StreamWriterBuilder{}, settings ) };
	DBBindings session_bindings;
	session_bindings.bind( id );
	session_bindings.bind( settings_json );
	DownloadSessionID session_id{};
	if ( permanent_session.has_value() )
	{
		session_id = permanent_session.value();
		session_bindings.bind( session_id );
		co_await transaction
				 .query(
					 //language=postgresql
					 R"SQL(
						 UPDATE
						     download_sessions
						 SET options = $2::jsonb,
						     last_used_at = now(),
						     is_subscription = true,
						     subscription_id = $1
						 WHERE download_session_id = $3
					 )SQL",
					 std::move( session_bindings ) )
				 .result();
	}
	else
	{
		const auto session{
			co_await transaction
					 .query(
						 //language=postgresql
						 R"SQL(
							 INSERT INTO download_sessions (download_session_id, name, options, is_subscription, subscription_id)
							 SELECT
							     id,
							     'Subscription ' || $1::text || ' / ' || id::text,
							     $2::jsonb,
							     true,
							     $1
							 FROM (
							     SELECT
							         nextval(pg_get_serial_sequence('download_sessions', 'download_session_id')) AS id
							 )
							     sequence
							 RETURNING download_session_id
						 )SQL",
						 std::move( session_bindings ) )
					 .rows< DownloadSessionID >()
		};
		session_id = std::get< 0 >( session.row( 0 ) );
	}
	ada::url_search_params query;
	for ( const auto& [ key, value ] : subscriptionParameters( values ) )
		query.append( key, value );
	const auto url{ ( origin.empty() ? "https://subscription.invalid/" : origin + "/" ) + "?" + query.to_string() };
	DBBindings root_bindings;
	root_bindings.bind( session_id );
	root_bindings.bind( url );
	const auto root{
		co_await transaction
				 .query(
					 //language=postgresql
					 R"SQL(
						 INSERT INTO download_session_urls (download_session_id, download_session_url_id, url)
						 SELECT
						     $1,
						     COALESCE(max(download_session_url_id), 0) + 1,
						     $2
						 FROM download_session_urls
						 WHERE download_session_id = $1
						 RETURNING download_session_url_id
					 )SQL",
					 std::move( root_bindings ) )
				 .rows< DownloadSessionUrlID >()
	};
	const auto root_id{ std::get< 0 >( root.row( 0 ) ) };
	DBBindings bindings;
	bindings.bind( id );
	bindings.bind( session_id );
	bindings.bind( root_id );
	co_await transaction
			 .query(
				 //language=postgresql
				 R"SQL(
					 UPDATE
					     subscriptions
					 SET session_id = $2,
					     active_session_id = $2,
					     last_session_id = $2,
					     active_root_id = $3,
					     last_error = NULL,
					     run_requested = false
					 WHERE subscription_id = $1
				 )SQL",
				 std::move( bindings ) )
			 .result();
	co_await transaction.commit();
	co_return SubscriptionRun{ id, session_id, url, settings_json, root_id };
}

runtime::Task< std::vector< DownloadSessionID > > subscriptionSessionIds( DBContext& db )
{
	const auto rows{
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
						 SELECT
						     download_session_id
						 FROM download_sessions
						 WHERE is_subscription
					 )SQL" )
				 .rows< DownloadSessionID >()
	};
	std::vector< DownloadSessionID > ids;
	for ( const auto& [ id ] : rows )
		ids.push_back( id );
	co_return ids;
}

runtime::Task< std::vector< SubscriptionGroupID > > subscriptionGroupsForSessions(
	DBContext& db,
	std::vector< DownloadSessionID > session_ids )
{
	DBBindings bindings;
	bindings.bind( std::move( session_ids ) );
	const auto rows{
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
						 SELECT DISTINCT
						     subscription_group_id
						 FROM subscriptions
						 WHERE session_id = ANY ($1::bigint[])
						 ORDER BY
						     subscription_group_id
					 )SQL",
					 std::move( bindings ) )
				 .rows< SubscriptionGroupID >()
	};
	std::vector< SubscriptionGroupID > result;
	for ( const auto& [ id ] : rows )
		result.push_back( id );
	co_return result;
}

runtime::Task< > refreshSubscriptionActivity( DBContext& db )
{
	// Missing source dates cannot establish that a search has been inactive for a year.
	// Dead subscriptions keep their schedule and can revive when a recent post appears.
	co_await db
			 .query(
				 //language=postgresql
				 R"SQL(
					 WITH activity AS (
					     SELECT
					         subscription_id,
					         initial_sync_complete
					         AND post_dates_complete
					         AND latest_post_at IS NOT NULL
					         AND latest_post_at <= now() - interval '1 year' AS dead
					     FROM subscriptions
					 )
					 UPDATE
					     subscriptions
					 SET dead = activity.dead
					 FROM activity
					 WHERE subscriptions.subscription_id = activity.subscription_id
					     AND subscriptions.dead IS DISTINCT FROM activity.dead
				 )SQL" )
			 .result();
}

runtime::Task< > finishSubscription( DBContext& db, const SubscriptionID id, const DownloadSessionID session_id )
{
	DBBindings bindings;
	bindings.bind( id );
	bindings.bind( session_id );
	co_await db
			 .query(
				 //language=postgresql
				 R"SQL(
					 WITH RECURSIVE run_urls AS (
					     SELECT
					         download_session_url_id,
					         record_id,
					         state,
					         posted_at,
					         post_dates_complete
					     FROM download_session_urls
					     WHERE download_session_id = $2
					         AND download_session_url_id = (
					             SELECT
					                 active_root_id
					             FROM subscriptions
					             WHERE subscription_id = $1
					         )
					     UNION ALL
					     SELECT
					         child.download_session_url_id,
					         child.record_id,
					         child.state,
					         child.posted_at,
					         child.post_dates_complete
					     FROM download_session_urls child
					         JOIN run_urls parent ON child.parent_url_id = parent.download_session_url_id
					     WHERE child.download_session_id = $2
					 ),
					 outcome AS (
					     SELECT
					         EXISTS (
					             SELECT
					                 1
					             FROM run_urls
					         )
					         AND NOT EXISTS (
					             SELECT
					                 1
					             FROM run_urls
					             WHERE state IN ('failed', 'cancelled')
					         ) AS success,
					         (
					             SELECT
					                 max(posted_at)
					             FROM run_urls
					             WHERE record_id IS NOT NULL
					         ) AS latest_post_at,
					             (
					                 SELECT
					                     COALESCE(bool_and(post_dates_complete), true)
					                 FROM run_urls
					                 WHERE record_id IS NOT NULL
					             ) AS dates_complete
					 ),
					 finished AS (
					     UPDATE
					         subscriptions
					     SET active_session_id = NULL,
					         active_root_id = NULL,
					         last_finished_at = now(),
					         next_run_at = now() + (
					             SELECT
					                 interval_seconds
					             FROM subscription_groups g
					             WHERE g.subscription_group_id = subscriptions.subscription_group_id
					         ) * interval '1 second',
					             initial_sync_complete = initial_sync_complete
					             OR outcome.success,
					             latest_post_at = GREATEST (subscriptions.latest_post_at, outcome.latest_post_at),
					             post_dates_complete = CASE WHEN outcome.success THEN outcome.dates_complete
					             ELSE
					                 subscriptions.post_dates_complete
					             END,
					             last_error = CASE WHEN outcome.success THEN NULL
					             ELSE
					                 COALESCE((
					                     SELECT
					                         message
					                     FROM download_session_url_failures f
					                     JOIN run_urls r ON r.download_session_url_id = f.download_session_url_id
					                     WHERE f.download_session_id = $2
					                     LIMIT 1
					                 ), 'Download run did not complete successfully')
					             END
					         FROM outcome
					         WHERE subscription_id = $1
					             AND active_session_id = $2
					             AND NOT EXISTS (
					                 SELECT
					                     1
					                 FROM run_urls
					                 WHERE state IN ('pending', 'processing')
					             )
					         RETURNING subscription_id
					 )
					 INSERT INTO subscriptions_log (subscription_id, record_id)
					 SELECT DISTINCT
					     finished.subscription_id,
					     run_urls.record_id
					 FROM finished
					     CROSS JOIN run_urls
					 WHERE run_urls.record_id IS NOT NULL
					     AND NOT EXISTS (
					         SELECT
					             1
					         FROM subscriptions_log
					         WHERE subscription_id = finished.subscription_id
					             AND record_id = run_urls.record_id
					     )
				 )SQL",
				 std::move( bindings ) )
			 .result();
}

//! The newest root identifies the current or most recently finished run in the permanent session.
static const std::string runUrlsSql{
	//language=postgresql
	R"SQL(
		WITH RECURSIVE latest AS (
		    SELECT
		        s.subscription_id,
		        s.session_id,
		        (
		            SELECT
		                max(download_session_url_id)
		            FROM download_session_urls u
		            WHERE u.download_session_id = s.session_id
		                AND u.parent_url_id IS NULL
		        ) AS root_id
		    FROM subscriptions s
		    WHERE s.subscription_id = $1
		),
		run_urls AS (
		    SELECT
		        u.download_session_url_id,
		        u.record_id,
		        u.state
		    FROM download_session_urls u
		        JOIN latest l ON u.download_session_id = l.session_id
		            AND u.download_session_url_id = l.root_id
		        UNION ALL
		        SELECT
		            c.download_session_url_id,
		            c.record_id,
		            c.state
		        FROM download_session_urls c
		        JOIN run_urls p ON c.parent_url_id = p.download_session_url_id
		    WHERE c.download_session_id = (
		            SELECT
		                session_id
		            FROM latest
		    )
		)
	)SQL"
};

runtime::Task< SubscriptionRunResult > subscriptionRunResult( DBContext& db, const SubscriptionID id )
{
	DBBindings bindings;
	bindings.bind( id );
	const auto rows{
		co_await db
				 .query(
					 runUrlsSql
					 +
					 //language=postgresql
					 R"SQL(
						 SELECT
						     session_id,
						     root_id,
						     (
						         SELECT
						             count(DISTINCT record_id)
						         FROM run_urls
						         WHERE state = 'imported'
						     ),
						     (
						         SELECT
						             count(DISTINCT record_id)
						         FROM run_urls
						         WHERE record_id IS NOT NULL
						             AND state <> 'imported'
						             AND record_id NOT IN (
						                 SELECT
						                     record_id
						                 FROM run_urls
						                 WHERE state = 'imported'
						                     AND record_id IS NOT NULL
						             )
						     )
						 FROM latest
					 )SQL",
					 std::move( bindings ) )
				 .rows<
					 std::optional< DownloadSessionID >,
					 std::optional< DownloadSessionUrlID >,
					 std::int64_t,
					 std::int64_t >()
	};
	if ( rows.empty() )
		co_return SubscriptionRunResult{ .subscription_id = id };

	const auto& [ session, root, imported, known ]{ rows.row( 0 ) };
	co_return SubscriptionRunResult{ id, session, root, imported, known };
}

runtime::Task< std::vector< RecordID > > subscriptionRunRecords( DBContext& db, const SubscriptionID id )
{
	DBBindings bindings;
	bindings.bind( id );
	const auto rows{
		co_await db
				 .query(
					 runUrlsSql
					 +
					 //language=postgresql
					 R"SQL(
						 SELECT DISTINCT
						     record_id
						 FROM run_urls
						 WHERE record_id IS NOT NULL
						     AND state = 'imported'
						 ORDER BY
						     record_id
					 )SQL",
					 std::move( bindings ) )
				 .rows< RecordID >()
	};
	std::vector< RecordID > result;
	for ( const auto& [ record ] : rows )
		result.push_back( record );
	co_return result;
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
