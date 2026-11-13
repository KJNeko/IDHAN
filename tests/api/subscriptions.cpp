#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <thread>

#include "ServerFixture.hpp"
#include "JobEventStream.hpp"

namespace idhan::test
{
static Json::Value subscriptionBody()
{
	Json::Value body { Json::objectValue };
	body[ "tags" ] = "artist_name bed";
	body[ "enabled" ] = false;
	return body;
}

static Json::Value groupBody()
{
	Json::Value body { Json::objectValue };
	body[ "name" ] = "Artists";
	body[ "generator" ] = "gelbooru:search";
	body[ "interval_seconds" ] = 90 * 86400;
	body[ "enabled" ] = false;
	return body;
}

static ApiResponse createTestSubscription( ApiClient& api, const Json::Value& body )
{
	auto group_body { groupBody() };
	group_body[ "enabled" ] = body[ "enabled" ];
	const auto group { api.post( "/subscription_groups", group_body ) };
	REQUIRE( group.status == 201 );
	return api.post( "/subscription_groups/" + group.json[ "id" ].asString() + "/subscriptions", body );
}

SCENARIO_METHOD(
	ServerFixture,
	"Groups own subscription names, generators, intervals and statistics",
	"[api][subscriptions]" )
{
	auto group_body { groupBody() };
	const auto group { api().post( "/subscription_groups", group_body ) };
	REQUIRE( group.status == 201 );
	const auto group_id { group.json[ "id" ].asInt() };
	const auto group_path { "/subscription_groups/" + std::to_string( group_id ) };
	auto body { subscriptionBody() };
	const auto created { api().post( group_path + "/subscriptions", body ) };
	INFO( created.body );
	REQUIRE( created.status == 201 );
	CHECK( created.json[ "group_id" ].asInt() == group_id );
	CHECK( created.json[ "tags" ] == body[ "tags" ] );
	CHECK( created.json[ "total_size_bytes" ].asInt64() == 0 );
	CHECK_FALSE( created.json.isMember( "name" ) );
	CHECK_FALSE( created.json.isMember( "interval_seconds" ) );
	CHECK_FALSE( created.json.isMember( "parser_key" ) );
	CHECK( api().post( group_path + "/subscriptions", body ).status == 409 );
	body[ "tags" ] = "other_artist bed";
	const auto second { api().post( group_path + "/subscriptions", body ) };
	REQUIRE( second.status == 201 );
	const auto listed { api().get( "/subscription_groups" ) };
	REQUIRE( listed.status == 200 );
	REQUIRE( listed.json.size() == 1 );
	CHECK( listed.json[ 0 ][ "subscription_count" ].asInt() == 2 );
	CHECK( listed.json[ 0 ][ "interval_seconds" ].asInt64() == 90 * 86400 );
	CHECK( api().postWithoutBody( group_path + "/run" ).status == 409 );

	group_body[ "interval_seconds" ] = 180 * 86400;
	const auto updated { api().patch( group_path, group_body ) };
	REQUIRE( updated.status == 200 );
	CHECK( updated.json[ "interval_seconds" ].asInt64() == 180 * 86400 );
	CHECK_FALSE( updated.json.isMember( "known_limit" ) );
	const auto record_id { api().createRecord( 900001 ) };
	const auto other_record_id { api().createRecord( 900004 ) };
	{
		pqxx::work transaction { db() };
		transaction.exec(
			"INSERT INTO subscriptions_log (subscription_id, record_id) VALUES ($1,$2),($3,$2)",
			pqxx::params { created.json[ "id" ].asInt(), record_id, second.json[ "id" ].asInt() } );
		transaction.exec(
			"INSERT INTO subscriptions_log (subscription_id, record_id) VALUES ($1,$2)",
			pqxx::params { second.json[ "id" ].asInt(), other_record_id } );
		// Equal-sized files must both count; the record shared by two queries counts once in the group.
		transaction.exec(
			"INSERT INTO file_info (record_id, size, extension) VALUES ($1, 1048576, 'png'),($2, 1048576, 'png')",
			pqxx::params { record_id, other_record_id } );
		transaction.commit();
	}
	const auto stats { api().get( "/subscription_groups" ) };
	CHECK( stats.json[ 0 ][ "record_count" ].asInt() == 2 );
	CHECK( stats.json[ 0 ][ "total_size_bytes" ].asInt64() == 2097152 );
	const auto items { api().get( "/subscriptions" ) };
	REQUIRE( items.status == 200 );
	REQUIRE( items.json.size() == 2 );
	CHECK( items.json[ 0 ][ "record_count" ].asInt() == 1 );
	CHECK( items.json[ 0 ][ "total_size_bytes" ].asInt64() == 1048576 );
	CHECK( items.json[ 1 ][ "record_count" ].asInt() == 2 );
	CHECK( items.json[ 1 ][ "total_size_bytes" ].asInt64() == 2097152 );
	CHECK( api().del( group_path ).status == 200 );
	CHECK( api().get( "/subscriptions" ).json.empty() );
	CHECK( api().get( "/subscription_groups" ).json.empty() );
}

SCENARIO_METHOD( ServerFixture, "Subscription groups reject invalid scheduling settings", "[api][subscriptions]" )
{
	auto body { groupBody() };
	SECTION( "Missing name" )
	{
		body[ "name" ] = "";
	}
	SECTION( "Unknown generator" )
	{
		body[ "generator" ] = "invalid";
	}
	SECTION( "Short interval" )
	{
		body[ "interval_seconds" ] = 0;
	}
	SECTION( "Long interval" )
	{
		body[ "interval_seconds" ] = 31536001;
	}
	CHECK( api().post( "/subscription_groups", body ).status == 400 );
}

SCENARIO_METHOD( ServerFixture, "Subscription queries remain strings without tag parsing", "[api][subscriptions]" )
{
	auto body { subscriptionBody() };
	SECTION( "Wrong tag type" )
	{
		body[ "tags" ] = 1;
		CHECK( createTestSubscription( api(), body ).status == 400 );
	}
	SECTION( "Preserves query text" )
	{
		body[ "tags" ] = "  cat_ears   nude,blue  ";
		const auto created { createTestSubscription( api(), body ) };
		REQUIRE( created.status == 201 );
		CHECK( created.json[ "tags" ] == body[ "tags" ] );
		CHECK( created.json[ "parameters" ][ "tags" ] == body[ "tags" ] );
	}
}

SCENARIO_METHOD(
	ServerFixture,
	"Scheduled subscription failures remain retryable without overlapping runs",
	"[api][subscriptions]" )
{
	// The API fixture has no Gelbooru route, so the scheduler can exercise failure handling without external requests.
	auto body { subscriptionBody() };
	body[ "enabled" ] = true;
	const auto created { createTestSubscription( api(), body ) };
	INFO( created.body );
	REQUIRE( created.status == 201 );
	const auto id { created.json[ "id" ].asInt() };
	const auto path { "/subscriptions/" + std::to_string( id ) };

	Json::Value finished;
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 20 } };
	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto listed { api().get( "/subscriptions" ) };
		REQUIRE( listed.status == 200 );
		REQUIRE( listed.json.size() == 1 );
		const auto& subscription { listed.json[ 0 ] };
		if ( !subscription[ "last_finished_at" ].isNull() )
		{
			finished = subscription;
			break;
		}

		std::this_thread::sleep_for( std::chrono::milliseconds { 100 } );
	}
	REQUIRE_FALSE( finished.isNull() );
	CHECK_FALSE( finished[ "initial_sync_complete" ].asBool() );
	CHECK( finished[ "active_session_id" ].isNull() );
	CHECK_FALSE( finished[ "last_error" ].isNull() );
	CHECK( finished[ "last_error" ].asString().contains( "Required downloader parser is not available" ) );
	CHECK( finished[ "next_run_at" ].asInt64() - finished[ "last_finished_at" ].asInt64() == 90 * 86400 );

	const auto session_id { finished[ "last_session_id" ].asInt64() };
	CHECK( api().get( "/download_sessions" ).json.empty() );
	const auto private_session { api().get( "/download_sessions/" + std::to_string( session_id ) ) };
	REQUIRE( private_session.status == 200 );
	CHECK( private_session.json[ "is_subscription" ].asBool() );
	{
		pqxx::work transaction { db() };
		const auto session { transaction.exec(
			"SELECT NOT (options ? 'subscription_known_limit') AND options #> '{parsers,gelbooru,knownLimit}' IS NULL "
			"FROM download_sessions WHERE download_session_id=$1",
			pqxx::params { session_id } ) };
		REQUIRE( session.size() == 1 );
		CHECK( session[ 0 ][ 0 ].as< bool >() );
		const auto root { transaction.exec(
			"SELECT url FROM download_session_urls WHERE download_session_id=$1 AND download_session_url_id=1",
			pqxx::params { session_id } ) };
		REQUIRE( root.size() == 1 );
		CHECK( root[ 0 ][ 0 ].as< std::string >() == "https://subscription.invalid/?tags=artist_name+bed" );
		// Keep a pending run for mutation/concurrency guards; the poller must not finish it.
		transaction.exec(
			"DELETE FROM download_session_url_failures WHERE download_session_id=$1", pqxx::params { session_id } );
		transaction.exec(
			"UPDATE download_session_urls SET state='pending', finished_at=NULL WHERE download_session_id=$1",
			pqxx::params { session_id } );
		transaction.exec(
			"UPDATE subscriptions SET active_session_id=$2, active_root_id=1 WHERE subscription_id=$1",
			pqxx::params { id, session_id } );
		transaction.commit();
	}
	CHECK( api().postWithoutBody( path + "/run" ).status == 409 );
	CHECK( api().del( path ).status == 409 );
	body[ "tags" ] = "artist_name bed new_query";
	CHECK( api().patch( path, body ).status == 409 );
	body[ "tags" ] = "artist_name bed";
	body[ "enabled" ] = false;
	CHECK( api().patch( path, body ).status == 200 );
}

static DownloadSessionID completedSubscriptionRun(
	pqxx::connection& db,
	const SubscriptionID id,
	const RecordID record_id,
	const std::optional< std::int64_t > post_age_seconds )
{
	pqxx::work transaction { db };
	const auto session { transaction.exec(
		"INSERT INTO download_sessions (name) VALUES ('subscription date fixture ' || gen_random_uuid()) "
		"RETURNING download_session_id" ) };
	const auto session_id { session[ 0 ][ 0 ].as< DownloadSessionID >() };
	transaction.exec(
		"INSERT INTO download_session_urls (download_session_id, download_session_url_id, url, state, finished_at, record_id, posted_at, post_dates_complete) "
		"VALUES ($1, 1, 'https://example.invalid/search', 'completed', now(), $2, now()-$3::bigint*interval '1 second', $3 IS NOT NULL)",
		pqxx::params { session_id, record_id, post_age_seconds } );
	transaction.exec(
		"INSERT INTO download_session_records (download_session_id, record_id, posted_at, post_dates_complete) "
		"VALUES ($1, $2, now() - $3::bigint * interval '1 second', $3 IS NOT NULL)",
		pqxx::params { session_id, record_id, post_age_seconds } );
	transaction.exec(
		"UPDATE subscriptions SET session_id=$2, active_session_id=$2, last_session_id=$2, active_root_id=1 WHERE subscription_id=$1",
		pqxx::params { id, session_id } );
	transaction.commit();
	return session_id;
}

static Json::Value waitForSubscriptionState(
	ApiClient& api,
	const SubscriptionID id,
	const DownloadSessionID session_id,
	const bool dead )
{
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 15 } };
	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto listed { api.get( "/subscriptions" ) };
		REQUIRE( listed.status == 200 );
		for ( const auto& subscription : listed.json )
		{
			if ( subscription[ "id" ].asInt() != id ) continue;

			if ( subscription[ "last_session_id" ].isNull() ) continue;

			if ( subscription[ "last_session_id" ].asInt64() != session_id ) continue;

			if ( !subscription[ "active_session_id" ].isNull() ) continue;

			if ( subscription[ "last_finished_at" ].isNull() ) continue;

			if ( subscription[ "dead" ].asBool() != dead ) continue;

			return subscription;
		}
		std::this_thread::sleep_for( std::chrono::milliseconds { 100 } );
	}
	throw std::runtime_error( "Timed out waiting for subscription date status" );
}

SCENARIO_METHOD(
	ServerFixture,
	"Dead subscriptions use source post dates and revive on recent posts",
	"[api][subscriptions]" )
{
	auto body { subscriptionBody() };
	const auto created { createTestSubscription( api(), body ) };
	REQUIRE( created.status == 201 );
	const auto id { created.json[ "id" ].as< SubscriptionID >() };
	const auto record_id { api().createRecord( 900002 ) };
	const auto old_run { completedSubscriptionRun( db(), id, record_id, 2 * 365 * 86400LL ) };
	const auto old { waitForSubscriptionState( api(), id, old_run, true ) };
	CHECK( old[ "initial_sync_complete" ].asBool() );
	CHECK( old[ "post_dates_complete" ].asBool() );
	CHECK( old[ "last_finished_at" ].asInt64() - old[ "latest_post_at" ].asInt64() >= 2 * 365 * 86400LL );
	{
		pqxx::nontransaction transaction { db() };
		CHECK(
			transaction.query_value< int >(
				"SELECT count(*) FROM subscriptions_log WHERE subscription_id=$1", pqxx::params { id } )
		    == 1 );
	}

	testServer().restart( true );
	const auto persisted { waitForSubscriptionState( api(), id, old_run, true ) };
	CHECK( persisted[ "latest_post_at" ] == old[ "latest_post_at" ] );
	CHECK_FALSE( persisted[ "enabled" ].asBool() );

	// Reusing a known file with a recent source date revives the search. Import time is always now().
	const auto recent_run { completedSubscriptionRun( db(), id, record_id, 30 * 86400LL ) };
	const auto recent { waitForSubscriptionState( api(), id, recent_run, false ) };
	CHECK( recent[ "latest_post_at" ].asInt64() > old[ "latest_post_at" ].asInt64() );
	{
		pqxx::nontransaction transaction { db() };
		CHECK(
			transaction.query_value< int >(
				"SELECT count(*) FROM subscriptions_log WHERE subscription_id=$1", pqxx::params { id } )
		    == 1 );
	}

	// The year threshold is refreshed even while the next download is months away.
	{
		pqxx::work transaction { db() };
		transaction.exec(
			"UPDATE subscriptions SET latest_post_at=now() - interval '1 year' - interval '1 second' WHERE subscription_id=$1",
			pqxx::params { id } );
		transaction.commit();
	}
	const auto aged { waitForSubscriptionState( api(), id, recent_run, true ) };
	CHECK( aged[ "next_run_at" ].asInt64() > aged[ "last_finished_at" ].asInt64() );

	// Changing a search discards its old activity evidence and schedules another full scan.
	body[ "tags" ] = "artist_name bed new_search";
	const auto changed { api().patch( "/subscriptions/" + std::to_string( id ), body ) };
	REQUIRE( changed.status == 200 );
	CHECK( changed.json[ "latest_post_at" ].isNull() );
	CHECK_FALSE( changed.json[ "dead" ].asBool() );
	CHECK_FALSE( changed.json[ "initial_sync_complete" ].asBool() );
}

SCENARIO_METHOD( ServerFixture, "Missing post dates do not mark subscriptions dead", "[api][subscriptions]" )
{
	const auto created { createTestSubscription( api(), subscriptionBody() ) };
	REQUIRE( created.status == 201 );
	const auto id { created.json[ "id" ].as< SubscriptionID >() };
	const auto record_id { api().createRecord( 900003 ) };
	const auto session_id { completedSubscriptionRun( db(), id, record_id, std::nullopt ) };
	const auto subscription { waitForSubscriptionState( api(), id, session_id, false ) };
	CHECK( subscription[ "latest_post_at" ].isNull() );
	CHECK_FALSE( subscription[ "post_dates_complete" ].asBool() );
	CHECK_FALSE( subscription[ "dead" ].asBool() );
}

SCENARIO_METHOD(
	ServerFixture,
	"Group migrations preserve already-applied subscription data",
	"[api][subscriptions][migration]" )
{
	pqxx::work transaction { db() };
	transaction.exec( "CREATE SCHEMA subscription_group_upgrade" );
	transaction.exec( "SET LOCAL search_path=subscription_group_upgrade,public" );
	transaction.exec(
		"CREATE TABLE subscriptions (subscription_id SERIAL PRIMARY KEY, name TEXT, parser_key TEXT, "
		"interval_seconds BIGINT, known_limit INTEGER, enabled BOOLEAN, values JSON, active_session_id BIGINT)" );
	transaction.exec(
		"INSERT INTO subscriptions VALUES (41, 'Existing artist', 'gelbooru:search', 7776000, 30, false, "
		"'{\"tags\":[\"artist_name\",\"bed\"]}', 99)" );
	for ( const auto* migration :
	      { "IDHANMigration/src/table_subscription_groups/456-create.sql",
	        "IDHANMigration/src/table_subscriptions/457-groups.sql" } )
	{
		std::ifstream input { std::string { IDHAN_SOURCE_DIR } + "/" + migration };
		REQUIRE( input.is_open() );
		const std::string sql { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };
		transaction.exec( sql );
	}
	const auto groups { transaction.exec( "SELECT * FROM subscription_groups" ) };
	REQUIRE( groups.size() == 1 );
	CHECK( groups[ 0 ][ "subscription_group_id" ].as< int >() == 41 );
	CHECK( groups[ 0 ][ "name" ].as< std::string >() == "Existing artist" );
	CHECK( groups[ 0 ][ "generator" ].as< std::string >() == "gelbooru:search" );
	CHECK( groups[ 0 ][ "interval_seconds" ].as< std::int64_t >() == 7776000 );
	CHECK( groups[ 0 ][ "known_limit" ].as< int >() == 30 );
	CHECK_FALSE( groups[ 0 ][ "enabled" ].as< bool >() );
	const auto items { transaction.exec( "SELECT *, values->'tags'->>1 AS second_tag FROM subscriptions" ) };
	REQUIRE( items.size() == 1 );
	CHECK( items[ 0 ][ "subscription_id" ].as< int >() == 41 );
	CHECK( items[ 0 ][ "subscription_group_id" ].as< int >() == 41 );
	CHECK( items[ 0 ][ "active_session_id" ].as< std::int64_t >() == 99 );
	CHECK( items[ 0 ][ "enabled" ].as< bool >() );
	CHECK( items[ 0 ][ "second_tag" ].as< std::string >() == "bed" );
	{
		std::ifstream input {
			std::string { IDHAN_SOURCE_DIR } + "/IDHANMigration/src/table_subscriptions/458-string-parameters.sql"
		};
		const std::string sql { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };
		transaction.exec( sql );
		CHECK(
			transaction.query_value< std::string >( "SELECT values->>'tags' FROM subscriptions" )
		    == "artist_name bed" );
	}

	CHECK(
		transaction.query_value< int >(
			"SELECT count(*) FROM information_schema.columns WHERE table_schema='subscription_group_upgrade' "
			"AND table_name='subscriptions' AND column_name IN ('name','parser_key','interval_seconds','known_limit')" )
	    == 0 );
	CHECK(
		transaction.query_value< int >(
			"INSERT INTO subscription_groups (name,generator) VALUES ('Next group','gelbooru:search') RETURNING subscription_group_id" )
	    > 41 );
	{
		std::ifstream input {
			std::string { IDHAN_SOURCE_DIR } + "/IDHANMigration/src/table_subscription_groups/465-remove-known-cutoff.sql"
		};
		REQUIRE( input.is_open() );
		const std::string sql { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };
		transaction.exec( sql );
		CHECK_FALSE( transaction.query_value< bool >(
			"SELECT EXISTS (SELECT 1 FROM pg_attribute WHERE attrelid='subscription_groups'::regclass "
			"AND attname='known_limit' AND NOT attisdropped)" ) );
		CHECK( transaction.query_value< std::int64_t >(
			"SELECT interval_seconds FROM subscription_groups WHERE subscription_group_id=41" ) == 7776000 );
	}
	transaction.abort();
}

SCENARIO_METHOD(
	ServerFixture,
	"Removing the known cutoff preserves other saved session options",
	"[api][subscriptions][migration]" )
{
	pqxx::work transaction { db() };
	transaction.exec( "CREATE TEMP TABLE download_sessions (options JSONB) ON COMMIT DROP" );
	transaction.exec(
		R"SQL(INSERT INTO download_sessions VALUES ('{"subscription_known_limit":20,"parsers":{"gelbooru":{"knownLimit":20,"pageLimit":2},"other":{"enabled":true}},"force_import":true}'))SQL" );
	std::ifstream input {
		std::string { IDHAN_SOURCE_DIR } + "/IDHANMigration/src/table_download_sessions/466-remove-known-cutoff.sql"
	};
	REQUIRE( input.is_open() );
	const std::string sql { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };
	transaction.exec( sql );
	CHECK( transaction.query_value< bool >(
		R"SQL(SELECT options='{"parsers":{"gelbooru":{"pageLimit":2},"other":{"enabled":true}},"force_import":true}'::jsonb FROM download_sessions)SQL" ) );
	transaction.abort();
}

SCENARIO_METHOD(
	ServerFixture,
	"Subscriptions invoke generators from any parser with raw parameters",
	"[api][subscriptions][generators]" )
{
	auto group_body { groupBody() };
	group_body[ "generator" ] = "fixture:echo";
	group_body[ "origin" ] = "https://example.invalid";
	group_body[ "enabled" ] = true;
	const auto group { api().post( "/subscription_groups", group_body ) };
	REQUIRE( group.status == 201 );
	const auto path { "/subscription_groups/" + group.json[ "id" ].asString() + "/subscriptions" };
	Json::Value body { Json::objectValue };
	body[ "parameters" ][ "query" ] = "  raw query, untouched  ";
	const auto first { api().post( path, body ) };
	REQUIRE( first.status == 201 );
	const auto id { first.json[ "id" ].as< SubscriptionID >() };
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 15 } };
	Json::Value finished;
	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto listed { api().get( "/subscriptions" ) };
		for ( const auto& item : listed.json )
		{
			if ( item[ "id" ].asInt() != id ) continue;

			if ( item[ "last_finished_at" ].isNull() ) continue;

			finished = item;
		}
		if ( !finished.isNull() ) break;

		std::this_thread::sleep_for( std::chrono::milliseconds { 100 } );
	}
	REQUIRE_FALSE( finished.isNull() );
	INFO( finished.toStyledString() );
	CHECK( finished[ "initial_sync_complete" ].asBool() );
	CHECK( finished[ "last_error" ].isNull() );
	CHECK( finished[ "parameters" ][ "query" ] == body[ "parameters" ][ "query" ] );
	CHECK( api().get( "/download_sessions" ).json.empty() );
	const auto permanent_id { finished[ "session_id" ].as< DownloadSessionID >() };
	{
		pqxx::work transaction { db() };
		transaction.exec(
			"UPDATE download_session_urls SET state='failed' WHERE download_session_id=$1 AND download_session_url_id=1",
			pqxx::params { permanent_id } );
		transaction.exec(
			"UPDATE subscriptions SET initial_sync_complete=false WHERE subscription_id=$1", pqxx::params { id } );
		transaction.commit();
	}
	testServer().restart( true );
	CHECK( api().get( "/download_sessions" ).json.empty() );
	REQUIRE( api().postWithoutBody( "/subscriptions/" + std::to_string( id ) + "/run" ).status == 200 );
	const auto repeated_deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 15 } };
	Json::Value repeated;
	while ( std::chrono::steady_clock::now() < repeated_deadline )
	{
		const auto listed { api().get( "/subscriptions" ) };
		for ( const auto& item : listed.json )
		{
			if ( item[ "id" ].asInt() != id ) continue;

			if ( item[ "last_finished_at" ] == finished[ "last_finished_at" ] ) continue;

			if ( !item[ "active_session_id" ].isNull() ) continue;

			repeated = item;
		}
		if ( !repeated.isNull() ) break;

		std::this_thread::sleep_for( std::chrono::milliseconds { 100 } );
	}
	REQUIRE_FALSE( repeated.isNull() );
	INFO( repeated.toStyledString() );
	CHECK( repeated[ "session_id" ].as< DownloadSessionID >() == permanent_id );
	CHECK( repeated[ "last_session_id" ].as< DownloadSessionID >() == permanent_id );
	CHECK( repeated[ "initial_sync_complete" ].asBool() );
	CHECK( repeated[ "last_error" ].isNull() );
	{
		pqxx::nontransaction transaction { db() };
		CHECK(
			transaction.query_value< int >(
				"SELECT count(*) FROM download_sessions WHERE subscription_id=$1", pqxx::params { id } )
		    == 1 );
		CHECK(
			transaction.query_value< int >(
				"SELECT count(*) FROM download_session_urls WHERE download_session_id=$1 AND parent_url_id IS NULL",
				pqxx::params { permanent_id } )
		    == 2 );
		const auto session { transaction.exec(
			"SELECT is_subscription, subscription_id FROM download_sessions WHERE download_session_id=$1",
			pqxx::params { finished[ "last_session_id" ].asInt64() } ) };
		REQUIRE( session.size() == 1 );
		CHECK( session[ 0 ][ 0 ].as< bool >() );
		CHECK( session[ 0 ][ 1 ].as< SubscriptionID >() == id );
	}
}
} // namespace idhan::test

namespace idhan::test
{
SCENARIO_METHOD( ServerFixture, "Bulk queries preserve raw parameters and skip duplicates", "[api][subscriptions]" )
{
	const auto group { api().post( "/subscription_groups", groupBody() ) };
	REQUIRE( group.status == 201 );
	const auto path { "/subscription_groups/" + group.json[ "id" ].asString() + "/subscriptions/batch" };
	Json::Value body;
	body[ "items" ] = Json::Value { Json::arrayValue };
	for ( const auto& tags : { "  artist bed  ", "artist_two,cat ears", "  artist bed  " } )
	{
		auto item { subscriptionBody() };
		item[ "tags" ] = tags;
		body[ "items" ].append( item );
	}
	const auto added { api().post( path, body ) };
	INFO( added.body );
	REQUIRE( added.status == 200 );
	CHECK( added.json[ "added" ].asInt() == 2 );
	CHECK( added.json[ "skipped" ].asInt() == 1 );
	const auto listed { api().get( "/subscriptions" ) };
	REQUIRE( listed.status == 200 );
	REQUIRE( listed.json.size() == 2 );
	CHECK( listed.json[ 0 ][ "tags" ].asString() == "  artist bed  " );
	CHECK( listed.json[ 1 ][ "tags" ].asString() == "artist_two,cat ears" );
	const auto repeated { api().post( path, body ) };
	CHECK( repeated.json[ "added" ].asInt() == 0 );
	CHECK( repeated.json[ "skipped" ].asInt() == 3 );
	body[ "items" ][ 0 ][ "tags" ] = std::string( 8193, 'a' );
	CHECK( api().post( path, body ).status == 400 );
	CHECK( api().get( "/subscriptions" ).json.size() == 2 );
}

SCENARIO_METHOD( ServerFixture, "Latest subscription run counts and files exclude old and known imports", "[api][subscriptions]" )
{
	const auto created { createTestSubscription( api(), subscriptionBody() ) };
	REQUIRE( created.status == 201 );
	const auto id { created.json[ "id" ].as< SubscriptionID >() };
	const auto old_record { api().createRecord( 910001 ) };
	const auto new_record { api().createRecord( 910002 ) };
	const auto known_record { api().createRecord( 910003 ) };
	{
		pqxx::work transaction { db() };
		const auto session { transaction.exec( "INSERT INTO download_sessions (name, is_subscription) VALUES ('run results',true) RETURNING download_session_id" ) };
		const auto session_id { session[ 0 ][ 0 ].as< DownloadSessionID >() };
		transaction.exec( "UPDATE subscriptions SET session_id=$2,last_session_id=$2 WHERE subscription_id=$1", pqxx::params { id, session_id } );
		transaction.exec(
			"INSERT INTO download_session_urls (download_session_id,download_session_url_id,parent_url_id,url,state,record_id,finished_at) VALUES "
			"($1,1,NULL,'https://example.invalid/old','completed',NULL,now()),"
			"($1,2,1,'https://example.invalid/old-file','imported',$2,now()),"
			"($1,3,NULL,'https://example.invalid/latest','completed',NULL,now()),"
			"($1,4,3,'https://example.invalid/new-file','imported',$3,now()),"
			"($1,5,3,'https://example.invalid/known-file','imported_previously',$4,now()),"
			"($1,6,3,'https://example.invalid/duplicate-file','imported',$3,now())",
			pqxx::params { session_id, old_record, new_record, known_record } );
		transaction.commit();
	}
	const auto listed { api().get( "/subscriptions" ) };
	REQUIRE( listed.status == 200 );
	CHECK( listed.json[ 0 ][ "imported_count" ].asInt() == 1 );
	CHECK( listed.json[ 0 ][ "known_count" ].asInt() == 1 );
	const auto files { api().get( "/subscriptions/" + std::to_string( id ) + "/records" ) };
	INFO( files.body );
	REQUIRE( files.status == 200 );
	REQUIRE( files.json[ "record_ids" ].size() == 1 );
	CHECK( files.json[ "record_ids" ][ 0 ].as< RecordID >() == new_record );
	CHECK( api().get( "/subscriptions/9999999/records" ).status == 404 );
	{
		pqxx::work transaction { db() };
		transaction.exec(
			"UPDATE subscriptions SET active_session_id=session_id,active_root_id=3 WHERE subscription_id=$1",
			pqxx::params { id } );
		// Keep the latest run active while checking that the stat still describes its predecessor.
		transaction.exec(
			"UPDATE download_session_urls SET state='pending',finished_at=NULL WHERE download_session_id="
			"(SELECT session_id FROM subscriptions WHERE subscription_id=$1) AND download_session_url_id=3",
			pqxx::params { id } );
		transaction.exec(
			"INSERT INTO download_session_urls (download_session_id,download_session_url_id,parent_url_id,url,state,record_id,finished_at) "
			"SELECT session_id,7,1,'https://example.invalid/another-old-file','imported',$2,now() FROM subscriptions WHERE subscription_id=$1",
			pqxx::params { id, known_record } );
		transaction.commit();
	}
	const auto running { api().get( "/subscriptions" ) };
	REQUIRE( running.status == 200 );
	CHECK( running.json[ 0 ][ "imported_count" ].asInt() == 2 );
	{
		pqxx::work transaction { db() };
		transaction.exec( "INSERT INTO subscriptions_log (subscription_id,record_id) VALUES ($1,$2),($1,$3)",
			pqxx::params { id, old_record, known_record } );
		transaction.exec( "INSERT INTO file_info (record_id,size,extension) VALUES ($1,1048576,'png'),($2,1048576,'png'),($3,1048576,'png')",
			pqxx::params { old_record, new_record, known_record } );
		transaction.commit();
	}
	const auto live { api().get( "/subscriptions" ) };
	REQUIRE( live.status == 200 );
	CHECK( live.json[ 0 ][ "record_count" ].asInt() == 3 );
	CHECK( live.json[ 0 ][ "total_size_bytes" ].asInt64() == 3 * 1048576 );
	CHECK( live.json[ 0 ][ "imported_count" ].asInt() == 2 );
	const auto live_group { api().get( "/subscription_groups" ) };
	REQUIRE( live_group.status == 200 );
	CHECK( live_group.json[ 0 ][ "record_count" ].asInt() == 3 );
	CHECK( live_group.json[ 0 ][ "total_size_bytes" ].asInt64() == 3 * 1048576 );
}
SCENARIO_METHOD( ServerFixture, "Subscription jobs stream their imported counts and final response", "[api][subscriptions]" )
{
	const auto created { createTestSubscription( api(), subscriptionBody() ) };
	REQUIRE( created.status == 201 );
	const auto id { created.json[ "id" ].as< SubscriptionID >() };
	const auto record { api().createRecord( 920001 ) };
	JobEventStream stream { serverPort(), api().key() };
	REQUIRE_FALSE( stream.waitFor( "snapshot" ).isNull() );
	const auto session { completedSubscriptionRun( db(), id, record, 60 ) };
	{
		pqxx::work transaction { db() };
		transaction.exec( "UPDATE download_session_urls SET state='imported' WHERE download_session_id=$1", pqxx::params { session } );
		transaction.commit();
	}
	const auto response { stream.waitFor( "response" ) };
	INFO( response.toStyledString() );
	REQUIRE_FALSE( response.isNull() );
	CHECK( response[ "response" ][ "kind" ].asString() == "subscription" );
	CHECK( response[ "response" ][ "subscription_id" ].as< SubscriptionID >() == id );
	CHECK( response[ "response" ][ "session_id" ].as< DownloadSessionID >() == session );
	CHECK( response[ "response" ][ "root_id" ].asInt() == 1 );
	CHECK( response[ "response" ][ "imported_count" ].asInt() == 1 );
	CHECK( response[ "response" ][ "known_count" ].asInt() == 0 );
	const auto status { api().get( "/jobs/" + response[ "job_id" ].asString() + "/status" ) };
	REQUIRE( status.status == 200 );
	CHECK( status.json[ "completed" ].asBool() );
	CHECK( status.json[ "progress" ][ "imported_count" ].asInt() == 1 );
	CHECK( status.json[ "response" ] == response[ "response" ] );
}

SCENARIO_METHOD( ServerFixture, "Subscription concurrency settings persist and reject invalid limits", "[api][subscriptions]" )
{
	const auto initial { api().get( "/subscriptions/settings" ) };
	REQUIRE( initial.status == 200 );
	CHECK( initial.json[ "max_running" ].asInt() == 4 );
	Json::Value body;
	for ( const auto invalid : { 0, -1, 101 } )
	{
		body[ "max_running" ] = invalid;
		CHECK( api().patch( "/subscriptions/settings", body ).status == 400 );
	}
	body[ "max_running" ] = 2;
	CHECK( api().patch( "/subscriptions/settings", body ).status == 200 );
	testServer().restart( true );
	const auto restored { api().get( "/subscriptions/settings" ) };
	REQUIRE( restored.status == 200 );
	CHECK( restored.json[ "max_running" ].asInt() == 2 );
}

SCENARIO_METHOD( ServerFixture, "Interrupted subscriptions wait for scheduler admission after restart", "[api][subscriptions][recovery]" )
{
	for ( int index {}; index < 6; ++index )
	{
		const auto created { createTestSubscription( api(), subscriptionBody() ) };
		REQUIRE( created.status == 201 );
		const auto record { api().createRecord( 940000 + index ) };
		const auto session { completedSubscriptionRun( db(), created.json[ "id" ].as< SubscriptionID >(), record, 60 ) };
		pqxx::work transaction { db() };
		transaction.exec( "UPDATE download_sessions SET is_subscription=true,subscription_id=$2 WHERE download_session_id=$1",
			pqxx::params { session, created.json[ "id" ].as< SubscriptionID >() } );
		transaction.exec( "UPDATE download_session_urls SET state='pending',finished_at=NULL "
			"WHERE download_session_id=$1 AND download_session_url_id=1", pqxx::params { session } );
		transaction.commit();
	}
	testServer().restart( true );
	const auto listed { api().get( "/subscriptions" ) };
	REQUIRE( listed.status == 200 );
	REQUIRE( listed.json.size() == 6 );
	for ( const auto& subscription : listed.json )
	{
		CHECK( subscription[ "active_session_id" ].isNull() );
		CHECK( subscription[ "active_root_id" ].as< DownloadSessionUrlID >() == 1 );
		CHECK( subscription[ "last_finished_at" ].isNull() );
	}
	pqxx::read_transaction transaction { db() };
	CHECK( transaction.query_value< int >(
		"SELECT count(*) FROM download_session_urls WHERE parent_url_id IS NULL AND state='pending'" ) == 6 );
}

SCENARIO_METHOD( ServerFixture, "Subscription groups run one search at a time", "[api][subscriptions][concurrency]" )
{
	Json::Value settings;
	settings[ "max_running" ] = 4;
	REQUIRE( api().patch( "/subscriptions/settings", settings ).status == 200 );
	auto group_body { groupBody() };
	group_body[ "generator" ] = "fixture:echo";
	group_body[ "origin" ] = "https://example.invalid";
	const auto group { api().post( "/subscription_groups", group_body ) };
	REQUIRE( group.status == 201 );
	const auto path { "/subscription_groups/" + group.json[ "id" ].asString() };
	Json::Value body;
	body[ "parameters" ][ "query" ] = "blocker";
	const auto blocker { api().post( path + "/subscriptions", body ) };
	REQUIRE( blocker.status == 201 );
	const auto record { api().createRecord( 950001 ) };
	const auto session { completedSubscriptionRun( db(), blocker.json[ "id" ].as< SubscriptionID >(), record, 60 ) };
	{
		pqxx::work transaction { db() };
		transaction.exec( "UPDATE download_session_urls SET state='pending',finished_at=NULL "
			"WHERE download_session_id=$1 AND download_session_url_id=1", pqxx::params { session } );
		transaction.commit();
	}
	body[ "parameters" ][ "query" ] = "  raw query, untouched  ";
	const auto queued { api().post( path + "/subscriptions", body ) };
	REQUIRE( queued.status == 201 );
	const auto queued_id { queued.json[ "id" ].as< SubscriptionID >() };
	group_body[ "enabled" ] = true;
	REQUIRE( api().patch( path, group_body ).status == 200 );
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 6 } };
	while ( std::chrono::steady_clock::now() < deadline )
	{
		pqxx::read_transaction transaction { db() };
		CHECK( transaction.query_value< bool >( "SELECT session_id IS NULL FROM subscriptions WHERE subscription_id=$1",
			pqxx::params { queued_id } ) );
		std::this_thread::sleep_for( std::chrono::milliseconds { 250 } );
	}
	{
		pqxx::work transaction { db() };
		transaction.exec( "UPDATE download_session_urls SET state='completed',finished_at=now() "
			"WHERE download_session_id=$1 AND download_session_url_id=1", pqxx::params { session } );
		transaction.commit();
	}
	bool finished {};
	const auto finish_deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 12 } };
	while ( std::chrono::steady_clock::now() < finish_deadline )
	{
		pqxx::read_transaction transaction { db() };
		finished = transaction.query_value< bool >( "SELECT last_finished_at IS NOT NULL FROM subscriptions WHERE subscription_id=$1",
			pqxx::params { queued_id } );
		if ( finished ) break;

		std::this_thread::sleep_for( std::chrono::milliseconds { 100 } );
	}
	CHECK( finished );
}

SCENARIO_METHOD( ServerFixture, "Subscription scheduler respects the global concurrency limit", "[api][subscriptions]" )
{
	Json::Value settings;
	settings[ "max_running" ] = 1;
	REQUIRE( api().patch( "/subscriptions/settings", settings ).status == 200 );
	const auto blocker { createTestSubscription( api(), subscriptionBody() ) };
	REQUIRE( blocker.status == 201 );
	const auto blocker_id { blocker.json[ "id" ].as< SubscriptionID >() };
	const auto record { api().createRecord( 930001 ) };
	const auto blocked_session { completedSubscriptionRun( db(), blocker_id, record, 60 ) };
	{
		pqxx::work transaction { db() };
		transaction.exec( "UPDATE download_session_urls SET state='pending',finished_at=NULL WHERE download_session_id=$1",
			pqxx::params { blocked_session } );
		transaction.commit();
	}
	auto group_body { groupBody() };
	group_body[ "generator" ] = "fixture:echo";
	group_body[ "enabled" ] = true;
	const auto group { api().post( "/subscription_groups", group_body ) };
	REQUIRE( group.status == 201 );
	Json::Value body;
	body[ "parameters" ][ "query" ] = "  raw query, untouched  ";
	const auto queued { api().post( "/subscription_groups/" + group.json[ "id" ].asString() + "/subscriptions", body ) };
	REQUIRE( queued.status == 201 );
	const auto queued_id { queued.json[ "id" ].as< SubscriptionID >() };
	// Observe more than one scheduler interval: the due query must remain unclaimed.
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 6 } };
	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto listed { api().get( "/subscriptions" ) };
		for ( const auto& item : listed.json )
		{
			if ( item[ "id" ].as< SubscriptionID >() != queued_id ) continue;

			REQUIRE( item[ "session_id" ].isNull() );
		}
		std::this_thread::sleep_for( std::chrono::milliseconds { 250 } );
	}
	settings[ "max_running" ] = 2;
	REQUIRE( api().patch( "/subscriptions/settings", settings ).status == 200 );
	const auto allowed_deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 12 } };
	bool started {};
	while ( std::chrono::steady_clock::now() < allowed_deadline )
	{
		const auto listed { api().get( "/subscriptions" ) };
		for ( const auto& item : listed.json )
		{
			if ( item[ "id" ].as< SubscriptionID >() == queued_id && !item[ "session_id" ].isNull() ) started = true;
			if ( item[ "id" ].as< SubscriptionID >() == blocker_id ) CHECK_FALSE( item[ "active_session_id" ].isNull() );
		}
		if ( started ) break;

		std::this_thread::sleep_for( std::chrono::milliseconds { 100 } );
	}
	CHECK( started );
}
} // namespace idhan::test
