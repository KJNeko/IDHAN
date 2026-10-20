#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <pqxx/pqxx>

#include <atomic>
#include <memory>
#include <string>
#include <unistd.h>

#include "TestConnection.hpp"
#include "idhan/db/DBConfig.hpp"
#include "initialize.hpp"

namespace idhan::test
{

// Extension fixtures need an isolated database to preserve the shared test database.
class InitializationDatabase
{
	pqxx::connection admin { connectionString() };
	std::string name;
	std::unique_ptr< pqxx::connection > database;

  public:

	InitializationDatabase()
	{
		static std::atomic< unsigned > counter {};
		name = "idhan_init_test_" + std::to_string( ::getpid() ) + "_" + std::to_string( ++counter );
		pqxx::nontransaction tx { admin };
		const auto can_create { tx.query_value< bool >(
			"SELECT rolcreatedb OR rolsuper FROM pg_catalog.pg_roles WHERE rolname = current_user" ) };
		if ( !can_create ) SKIP( "Database initialization tests require a PostgreSQL role with CREATEDB" );
		tx.exec( "CREATE DATABASE " + tx.quote_name( name ) + " TEMPLATE template0" );
		try
		{
			database = std::make_unique< pqxx::connection >( connectionString() + " dbname=" + name );
		}
		catch ( ... )
		{
			tx.exec( "DROP DATABASE " + tx.quote_name( name ) );
			throw;
		}
	}

	~InitializationDatabase()
	{
		database.reset();
		try
		{
			pqxx::nontransaction tx { admin };
			tx.exec( "DROP DATABASE " + tx.quote_name( name ) );
		}
		catch ( const std::exception& error )
		{
			WARN( "Could not remove initialization test database " << name << ": " << error.what() );
		}
	}

	pqxx::connection& connection() { return *database; }
};

TEST_CASE( "Database defaults and search paths keep application and extension schemas distinct", "[db][schema]" )
{
	CHECK( db::DBConfig {}.schema == "idhan" );
	CHECK( db::makeSearchPath( "idhan" ) == "\"idhan\",public" );
	CHECK( db::makeSearchPath( "public" ) == "\"public\"" );
	CHECK( db::makeSearchPath( "Mixed Case,\"schema" ) == "\"Mixed Case,\"\"schema\",public" );
	CHECK_THROWS_AS( db::makeSearchPath( "" ), std::invalid_argument );
	CHECK_THROWS_AS( db::makeSearchPath( std::string { "a\0b", 3 } ), std::invalid_argument );
}

// Only test setup provisions extensions; application initialization must leave them untouched.
void installTestExtensions( pqxx::transaction_base& tx )
{
	tx.exec(
		"CREATE EXTENSION pgcrypto WITH SCHEMA public; "
		"CREATE EXTENSION pg_trgm WITH SCHEMA public; "
		"CREATE EXTENSION pg_stat_statements WITH SCHEMA public; "
		"CREATE EXTENSION vector WITH SCHEMA public" );
}

std::string installedExtensions( pqxx::transaction_base& tx )
{
	return tx.query_value< std::string >(
		"SELECT string_agg(oid::text || ':' || extname || ':' || extnamespace::text || ':' || extversion, "
		"';' ORDER BY extname) FROM pg_catalog.pg_extension" );
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Administrator-provisioned extensions remain unchanged during application initialization",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	installTestExtensions( tx );
	const auto extensions { installedExtensions( tx ) };
	db::initializeDatabase( tx, "idhan" );
	CHECK( tx.query_value< std::string >( "SELECT current_schema()" ) == "idhan" );
	tx.exec( "CREATE TABLE schema_probe (id integer)" );
	CHECK(
		tx.query_value< std::string >(
			"SELECT n.nspname FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace WHERE c.relname = 'schema_probe'" )
		== "idhan" );
	CHECK( tx.query_value< double >( "SELECT B'1010' <~> B'1000'" ) == 1.0 );
	CHECK( tx.query_value< double >( "SELECT '[1,0]'::halfvec <=> '[1,0]'::halfvec" ) == 0.0 );
	db::initializeDatabase( tx, "idhan" );
	CHECK( installedExtensions( tx ) == extensions );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"An empty database reports every missing extension without installing anything",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	const auto extensions { installedExtensions( tx ) };
	for ( const auto name : { "pgcrypto", "pg_trgm", "pg_stat_statements", "vector" } )
	{
		CHECK_THROWS_WITH(
			db::initializeDatabase( tx, "idhan" ),
			Catch::Matchers::ContainsSubstring(
				"CREATE EXTENSION \"" + std::string { name } + "\" WITH SCHEMA public;" ) );
	}
	CHECK( installedExtensions( tx ) == extensions );
	CHECK_FALSE( tx.query_value< bool >( "SELECT EXISTS (SELECT 1 FROM pg_namespace WHERE nspname = 'idhan')" ) );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Each missing extension prevents startup while preserving unrelated data",
	"[db][schema][bootstrap]" )
{
	const std::string missing { GENERATE( "pgcrypto", "pg_trgm", "pg_stat_statements", "vector" ) };
	pqxx::work tx { connection() };
	installTestExtensions( tx );
	tx.exec( "DROP EXTENSION " + tx.quote_name( missing ) );
	tx.exec(
		"CREATE SCHEMA other_app; CREATE TABLE other_app.records (id integer); "
		"INSERT INTO other_app.records VALUES (42)" );
	const auto extensions { installedExtensions( tx ) };
	CHECK_THROWS_WITH(
		db::initializeDatabase( tx, "idhan" ), Catch::Matchers::ContainsSubstring( "Missing " + missing ) );
	CHECK( installedExtensions( tx ) == extensions );
	CHECK( tx.query_value< int >( "SELECT id FROM other_app.records" ) == 42 );
	CHECK_FALSE( tx.query_value< bool >( "SELECT EXISTS (SELECT 1 FROM pg_namespace WHERE nspname = 'idhan')" ) );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Application initialization coexists with unrelated schemas and tables",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	installTestExtensions( tx );
	tx.exec(
		"CREATE SCHEMA other_app; CREATE SCHEMA another_app; "
		"CREATE TABLE other_app.records (id integer); INSERT INTO other_app.records VALUES (42); "
		"CREATE TABLE public.records (id integer); INSERT INTO public.records VALUES (84); "
		"CREATE VIEW other_app.record_view AS SELECT * FROM other_app.records; "
		"CREATE SEQUENCE public.user_sequence START WITH 17" );
	const auto extensions { installedExtensions( tx ) };
	db::initializeDatabase( tx, "idhan" );
	tx.exec( "CREATE TABLE records (id integer); INSERT INTO records VALUES (21)" );
	CHECK( tx.query_value< std::string >( "SELECT current_schema()" ) == "idhan" );
	CHECK( tx.query_value< int >( "SELECT id FROM records" ) == 21 );
	CHECK( tx.query_value< int >( "SELECT id FROM other_app.records" ) == 42 );
	CHECK( tx.query_value< int >( "SELECT id FROM public.records" ) == 84 );
	CHECK( tx.query_value< int >( "SELECT id FROM other_app.record_view" ) == 42 );
	CHECK( tx.query_value< int >( "SELECT nextval('public.user_sequence')" ) == 17 );
	db::initializeDatabase( tx, "idhan" );
	CHECK( installedExtensions( tx ) == extensions );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Existing public installations require an explicit schema setting",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	installTestExtensions( tx );
	tx.exec(
		"CREATE TABLE public.idhan_info (object_name text PRIMARY KEY); "
		"INSERT INTO public.idhan_info VALUES ('table_records'); "
		"CREATE TABLE public.records (id integer); INSERT INTO public.records VALUES (42)" );
	CHECK_THROWS_WITH(
		db::initializeDatabase( tx, "idhan" ), Catch::Matchers::ContainsSubstring( "IDHAN_DATABASE_SCHEMA=public" ) );
	CHECK_FALSE( tx.query_value< bool >( "SELECT EXISTS (SELECT 1 FROM pg_namespace WHERE nspname = 'idhan')" ) );
	db::initializeDatabase( tx, "public" );
	CHECK( tx.query_value< std::string >( "SELECT current_schema()" ) == "public" );
	CHECK( tx.query_value< int >( "SELECT id FROM records" ) == 42 );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Custom schema identifiers preserve extension lookup",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	installTestExtensions( tx );
	const std::string schema { "Mixed Case,\"schema" };
	db::initializeDatabase( tx, schema );
	CHECK( tx.query_value< std::string >( "SELECT current_schema()" ) == schema );
	CHECK( tx.query_value< double >( "SELECT B'1010' <~> B'1000'" ) == 1.0 );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Extensions outside the search path are reported without relocation",
	"[db][schema][bootstrap]" )
{
	const std::string extension { GENERATE( "pgcrypto", "pg_trgm", "pg_stat_statements", "vector" ) };
	pqxx::work tx { connection() };
	installTestExtensions( tx );
	tx.exec( "CREATE SCHEMA extensions; ALTER EXTENSION " + tx.quote_name( extension ) + " SET SCHEMA extensions" );
	const auto extensions { installedExtensions( tx ) };
	CHECK_THROWS_WITH(
		db::initializeDatabase( tx, "idhan" ),
		Catch::Matchers::ContainsSubstring( "Extension " + extension + " is installed in" ) );
	CHECK( installedExtensions( tx ) == extensions );
	CHECK_FALSE( tx.query_value< bool >( "SELECT EXISTS (SELECT 1 FROM pg_namespace WHERE nspname = 'idhan')" ) );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Existing application-schema extensions remain supported",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	installTestExtensions( tx );
	tx.exec(
		"CREATE SCHEMA legacy_app; ALTER EXTENSION pgcrypto SET SCHEMA legacy_app; "
		"ALTER EXTENSION pg_trgm SET SCHEMA legacy_app; ALTER EXTENSION pg_stat_statements SET SCHEMA legacy_app" );
	const auto extensions { installedExtensions( tx ) };
	db::initializeDatabase( tx, "legacy_app" );
	CHECK( tx.query_value< std::string >( "SELECT current_schema()" ) == "legacy_app" );
	CHECK( tx.query_value< int >( "SELECT length(gen_random_bytes(32))" ) == 32 );
	CHECK( tx.query_value< double >( "SELECT similarity('test', 'test')" ) == 1.0 );
	CHECK( installedExtensions( tx ) == extensions );
	tx.commit();
}

} // namespace idhan::test
