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

SCENARIO_METHOD( ServerFixture, "Groups own subscription names, generators and intervals", "[api][subscriptions]" )
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
	CHECK_FALSE( created.json.isMember( "total_size_bytes" ) );
	CHECK_FALSE( created.json.isMember( "active_root_id" ) );
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
	"Group migrations preserve already-applied subscription data",
	"[api][subscriptions][migration]" )
{
	pqxx::work transaction { db() };
	transaction.exec(
		//language=postgresql
		R"SQL(
			CREATE SCHEMA subscription_group_upgrade
		)SQL" );
	transaction.exec(
		//language=postgresql
		R"SQL(
			SET LOCAL search_path = subscription_group_upgrade, public
		)SQL" );
	transaction.exec(
		//language=postgresql
		R"SQL(
			CREATE TABLE subscriptions (
			    subscription_id   SERIAL PRIMARY KEY,
			    name              TEXT,
			    parser_key        TEXT,
			    interval_seconds  BIGINT,
			    known_limit       INTEGER,
			    enabled           BOOLEAN,
			    values            JSON,
			    active_session_id BIGINT
			)
		)SQL" );
	transaction.exec(
		//language=postgresql
		R"SQL(
			INSERT INTO subscriptions
			    VALUES (41, 'Existing artist', 'gelbooru:search', 7776000, 30, false, '{"tags":["artist_name","bed"]}', 99)
		)SQL" );
	for ( const auto* migration :
	      { "IDHANMigration/src/table_subscription_groups/456-create.sql",
	        "IDHANMigration/src/table_subscriptions/457-groups.sql" } )
	{
		std::ifstream input { std::string { IDHAN_SOURCE_DIR } + "/" + migration };
		REQUIRE( input.is_open() );
		const std::string sql { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };
		transaction.exec( sql );
	}
	const auto groups{
		transaction.exec(
			//language=postgresql
			R"SQL(
			SELECT
			    *
			FROM subscription_groups
		)SQL" )
	};
	REQUIRE( groups.size() == 1 );
	CHECK( groups[ 0 ][ "subscription_group_id" ].as< int >() == 41 );
	CHECK( groups[ 0 ][ "name" ].as< std::string >() == "Existing artist" );
	CHECK( groups[ 0 ][ "generator" ].as< std::string >() == "gelbooru:search" );
	CHECK( groups[ 0 ][ "interval_seconds" ].as< std::int64_t >() == 7776000 );
	CHECK( groups[ 0 ][ "known_limit" ].as< int >() == 30 );
	CHECK_FALSE( groups[ 0 ][ "enabled" ].as< bool >() );
	const auto items{
		transaction.exec(
			//language=postgresql
			R"SQL(
			SELECT
			    *,
			    values -> 'tags' ->> 1 AS second_tag
			FROM subscriptions
		)SQL" )
	};
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
			transaction.query_value< std::string >(
				//language=postgresql
				R"SQL(
					SELECT
					    values ->> 'tags'
					FROM subscriptions
				)SQL" )
			== "artist_name bed" );
	}

	CHECK(
		transaction.query_value< int >(
			//language=postgresql
			R"SQL(
				SELECT
				    count(*)
				FROM information_schema.columns
				WHERE table_schema = 'subscription_group_upgrade'
				    AND table_name = 'subscriptions'
				    AND column_name IN ('name', 'parser_key', 'interval_seconds', 'known_limit')
			)SQL" )
		== 0 );
	CHECK(
		transaction.query_value< int >(
			//language=postgresql
			R"SQL(
				INSERT INTO subscription_groups (name, generator)
				    VALUES ('Next group', 'gelbooru:search')
				RETURNING subscription_group_id
			)SQL" )
		> 41 );
	{
		std::ifstream input {
			std::string { IDHAN_SOURCE_DIR } + "/IDHANMigration/src/table_subscription_groups/465-remove-known-cutoff.sql"
		};
		REQUIRE( input.is_open() );
		const std::string sql { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };
		transaction.exec( sql );
		CHECK_FALSE( transaction.query_value< bool >(
			//language=postgresql
			R"SQL(
				SELECT
				    EXISTS (
				        SELECT
				            1
				        FROM pg_attribute
				        WHERE attrelid = 'subscription_groups'::regclass
				            AND attname = 'known_limit'
				            AND NOT attisdropped
				    )
			)SQL" ) );
		CHECK(
			transaction.query_value< std::int64_t >(
				//language=postgresql
				R"SQL(
					SELECT
					    interval_seconds
					FROM subscription_groups
					WHERE subscription_group_id = 41
				)SQL" )
			== 7776000 );
	}
	transaction.abort();
}

SCENARIO_METHOD(
	ServerFixture,
	"Removing the known cutoff preserves other saved session options",
	"[api][subscriptions][migration]" )
{
	pqxx::work transaction { db() };
	transaction.exec(
		//language=postgresql
		R"SQL(
			CREATE TEMP TABLE download_sessions (
			    options JSONB
			) ON COMMIT DROP
		)SQL" );
	transaction.exec(
		//language=postgresql
		R"SQL(
			INSERT INTO download_sessions
			    VALUES ('{"subscription_known_limit":20,"parsers":{"gelbooru":{"knownLimit":20,"pageLimit":2},"other":{"enabled":true}},"force_import":true}')
		)SQL" );
	std::ifstream input {
		std::string { IDHAN_SOURCE_DIR } + "/IDHANMigration/src/table_download_sessions/466-remove-known-cutoff.sql"
	};
	REQUIRE( input.is_open() );
	const std::string sql { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };
	transaction.exec( sql );
	CHECK( transaction.query_value< bool >(
		//language=postgresql
		R"SQL(
			SELECT
			    options = '{"parsers":{"gelbooru":{"pageLimit":2},"other":{"enabled":true}},"force_import":true}'::jsonb
			FROM download_sessions
		)SQL" ) );
	transaction.abort();
}

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

static Json::Value waitForSubscriptionCheck( ApiClient& api, const SubscriptionID id, const std::int64_t after = 0 )
{
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 75 } };
	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto listed { api.get( "/subscriptions" ) };
		REQUIRE( listed.status == 200 );
		for ( const auto& item : listed.json )
		{
			if ( item[ "id" ].as< SubscriptionID >() != id ) continue;

			if ( !item[ "active_session_id" ].isNull() ) continue;

			if ( item[ "last_finished_at" ].isNull() ) continue;

			if ( item[ "last_finished_at" ].asInt64() <= after ) continue;

			return item;
		}
		std::this_thread::sleep_for( std::chrono::milliseconds { 100 } );
	}
	throw std::runtime_error( "Timed out waiting for subscription check" );
}

SCENARIO_METHOD(
	ServerFixture,
	"Subscriptions retry the same session and record downloader outcomes",
	"[api][subscriptions][scheduler]" )
{
	auto group_body { groupBody() };
	group_body[ "enabled" ] = true;
	group_body[ "generator" ] = "fixture:echo";
	group_body[ "origin" ] = "https://example.invalid";
	const auto group { api().post( "/subscription_groups", group_body ) };
	REQUIRE( group.status == 201 );
	Json::Value body;
	body[ "parameters" ][ "query" ] = "  raw query, untouched  ";
	const auto created {
		api().post( "/subscription_groups/" + group.json[ "id" ].asString() + "/subscriptions", body )
	};
	REQUIRE( created.status == 201 );
	const auto id { created.json[ "id" ].as< SubscriptionID >() };
	const auto finished { waitForSubscriptionCheck( api(), id ) };
	INFO( finished.toStyledString() );
	CHECK( finished[ "last_error" ].isNull() );
	CHECK( finished[ "parameters" ] == body[ "parameters" ] );
	CHECK( finished[ "next_run_at" ].asInt64() - finished[ "last_finished_at" ].asInt64() == 90 * 86400 );
	const auto session_id { finished[ "session_id" ].as< DownloadSessionID >() };
	const auto result { api().get( "/download_sessions/" + std::to_string( session_id ) + "/result" ) };
	REQUIRE( result.status == 200 );
	CHECK( result.json[ "imported_count" ].asInt64() == 0 );
	CHECK( result.json[ "known_count" ].asInt64() == 0 );
	CHECK( result.json[ "error" ].isNull() );
	REQUIRE( api().postWithoutBody( "/subscriptions/" + std::to_string( id ) + "/run" ).status == 200 );
	const auto repeated { waitForSubscriptionCheck( api(), id, finished[ "last_finished_at" ].asInt64() ) };
	CHECK( repeated[ "session_id" ] == finished[ "session_id" ] );
	CHECK( repeated[ "last_error" ].isNull() );
	pqxx::read_transaction transaction { db() };
	CHECK(
		transaction.query_value< int >(
			"SELECT count(*) FROM download_sessions WHERE subscription_id = $1", pqxx::params { id } )
	    == 1 );
	CHECK(
		transaction.query_value< int >(
			"SELECT count(*) FROM download_session_urls WHERE download_session_id = $1 AND parent_url_id IS NULL",
			pqxx::params { session_id } )
	    == 1 );
}

SCENARIO_METHOD(
	ServerFixture,
	"A failed subscription remains scheduled with its downloader error",
	"[api][subscriptions][scheduler]" )
{
	auto body { subscriptionBody() };
	body[ "enabled" ] = true;
	const auto created { createTestSubscription( api(), body ) };
	REQUIRE( created.status == 201 );
	const auto finished { waitForSubscriptionCheck( api(), created.json[ "id" ].as< SubscriptionID >() ) };
	CHECK( finished[ "active_session_id" ].isNull() );
	CHECK( finished[ "last_error" ].asString().contains( "Required downloader parser is not available" ) );
	CHECK( finished[ "next_run_at" ].asInt64() - finished[ "last_finished_at" ].asInt64() == 90 * 86400 );
}

SCENARIO_METHOD(
	ServerFixture,
	"Each timer tick admits one due item per idle group",
	"[api][subscriptions][scheduler]" )
{
	auto body { subscriptionBody() };
	body[ "enabled" ] = true;
	auto group_body { groupBody() };
	const auto first_group { api().post( "/subscription_groups", group_body ) };
	const auto second_group { api().post( "/subscription_groups", group_body ) };
	REQUIRE( first_group.status == 201 );
	REQUIRE( second_group.status == 201 );
	std::vector< SubscriptionID > ids;
	for ( const auto& group : { first_group, second_group } )
	{
		for ( const auto* tags : { "first", "second" } )
		{
			body[ "tags" ] = tags;
			const auto item {
				api().post( "/subscription_groups/" + group.json[ "id" ].asString() + "/subscriptions", body )
			};
			REQUIRE( item.status == 201 );
			ids.push_back( item.json[ "id" ].as< SubscriptionID >() );
		}
	}
	{
		pqxx::work transaction { db() };
		transaction.exec( "UPDATE subscription_groups SET enabled = true" );
		transaction.commit();
	}
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 40 } };
	bool admitted {};
	while ( std::chrono::steady_clock::now() < deadline )
	{
		pqxx::read_transaction transaction { db() };
		const auto count {
			transaction.query_value< int >( "SELECT count(*) FROM subscriptions WHERE active_session_id IS NOT NULL" )
		};
		if ( count == 2 )
		{
			CHECK(
				transaction.query_value< int >(
					"SELECT count(DISTINCT subscription_group_id) FROM subscriptions WHERE active_session_id IS NOT NULL" )
			    == 2 );
			CHECK(
				transaction.query_value< int >( "SELECT count(*) FROM subscriptions WHERE session_id IS NULL" ) == 2 );
			admitted = true;
			break;
		}
		std::this_thread::sleep_for( std::chrono::milliseconds { 100 } );
	}
	REQUIRE( admitted );
}

SCENARIO_METHOD(
	ServerFixture,
	"Subscription restart recovery distinguishes completed and interrupted checks",
	"[api][subscriptions][subscription-recovery]" )
{
	const auto created { createTestSubscription( api(), subscriptionBody() ) };
	REQUIRE( created.status == 201 );
	const auto id { created.json[ "id" ].as< SubscriptionID >() };
	DownloadSessionID session_id {};
	{
		pqxx::work transaction { db() };
		session_id = transaction.query_value< DownloadSessionID >(
			"INSERT INTO download_sessions (name, is_subscription, subscription_id) VALUES ('saved check', true, $1) RETURNING download_session_id",
			pqxx::params { id } );
		transaction.exec(
			"INSERT INTO download_session_urls (download_session_id, download_session_url_id, url, state, finished_at) VALUES ($1, 1, 'https://example.invalid/', 'completed', now())",
			pqxx::params { session_id } );
		transaction.exec(
			"INSERT INTO download_session_results (download_session_id, imported_count, known_count) VALUES ($1, 0, 0)",
			pqxx::params { session_id } );
		transaction.exec(
			"UPDATE subscriptions SET session_id = $2, active_session_id = $2 WHERE subscription_id = $1",
			pqxx::params { id, session_id } );
		transaction.commit();
	}
	testServer().restart( true );
	const auto completed { waitForSubscriptionCheck( api(), id ) };
	CHECK( completed[ "last_error" ].isNull() );
	CHECK( completed[ "session_id" ].as< DownloadSessionID >() == session_id );
	{
		pqxx::work transaction { db() };
		transaction.exec(
			"DELETE FROM download_session_results WHERE download_session_id = $1", pqxx::params { session_id } );
		transaction.exec(
			"UPDATE download_session_urls SET state = 'pending', finished_at = NULL WHERE download_session_id = $1",
			pqxx::params { session_id } );
		transaction.exec(
			"UPDATE subscriptions SET active_session_id = $2 WHERE subscription_id = $1",
			pqxx::params { id, session_id } );
		transaction.commit();
	}
	testServer().restart( true );
	const auto listed { api().get( "/subscriptions" ) };
	REQUIRE( listed.status == 200 );
	REQUIRE( listed.json.size() == 1 );
	CHECK( listed.json[ 0 ][ "active_session_id" ].isNull() );
	CHECK( listed.json[ 0 ][ "last_error" ] == "Interrupted by server restart" );
	CHECK( listed.json[ 0 ][ "session_id" ].as< DownloadSessionID >() == session_id );
	CHECK( listed.json[ 0 ][ "parameters" ] == created.json[ "parameters" ] );
}

} // namespace idhan::test
