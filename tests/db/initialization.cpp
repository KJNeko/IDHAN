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

//! Extension fixtures need an isolated database to preserve the shared test database.
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
				"SELECT\n"
				"    rolcreatedb\n"
				"    OR rolsuper\n"
				"FROM pg_catalog.pg_roles\n"
				"WHERE rolname = current_user" )
		};
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
	CHECK( db::makeSearchPath( "idhan" ) == "\"idhan\",\"public\"" );
	CHECK( db::makeSearchPath( "public" ) == "\"public\"" );
	CHECK( db::makeSearchPath( "Mixed Case,\"schema" ) == "\"Mixed Case,\"\"schema\",\"public\"" );
	CHECK( db::DBConfig {}.extension_schema == "public" );
	CHECK( db::makeSearchPath( "idhan", "extensions" ) == "\"idhan\",\"extensions\"" );
	CHECK( db::makeSearchPath( "idhan", "idhan" ) == "\"idhan\"" );
	CHECK( db::makeSearchPath( "idhan", "Mixed,\"extensions" ) == "\"idhan\",\"Mixed,\"\"extensions\"" );
	CHECK_THROWS_AS( db::makeSearchPath( "idhan", "" ), std::invalid_argument );
	CHECK_THROWS_AS( db::makeSearchPath( "idhan", std::string { "a\0b", 3 } ), std::invalid_argument );
	CHECK_THROWS_AS( db::makeSearchPath( "" ), std::invalid_argument );
	CHECK_THROWS_AS( db::makeSearchPath( std::string { "a\0b", 3 } ), std::invalid_argument );
}

void requireAvailableExtensions( pqxx::transaction_base& tx )
{
	for ( const auto name : { "pgcrypto", "pg_trgm", "pg_stat_statements", "vector" } )
	{
		const bool available { tx.query_value< bool >(
			"SELECT\n"
			"    EXISTS (\n"
			"        SELECT\n"
			"            1\n"
			"        FROM pg_catalog.pg_available_extensions\n"
			"        WHERE name = $1\n"
			"    )",
			pqxx::params { name } ) };
		if ( !available ) SKIP( "PostgreSQL extension supporting files are unavailable: " << name );
	}
}

//! Fixtures provision existing extensions to verify their identities remain unchanged.
void installTestExtensions( pqxx::transaction_base& tx )
{
	requireAvailableExtensions( tx );
	tx.exec(
		"CREATE EXTENSION pgcrypto WITH SCHEMA public;\n"
		"\n"
		"CREATE EXTENSION pg_trgm WITH SCHEMA public;\n"
		"\n"
		"CREATE EXTENSION pg_stat_statements WITH SCHEMA public;\n"
		"\n"
		"CREATE EXTENSION vector WITH SCHEMA public" );
}

std::string installedExtensions( pqxx::transaction_base& tx )
{
	return tx.query_value< std::string >(
		"SELECT string_agg(oid::text || ':' || extname || ':' || extnamespace::text || ':' || extversion, ';'\n"
		"                  ORDER BY extname)\n"
		"FROM pg_catalog.pg_extension" );
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
	CHECK( tx.query_value< std::string >( "SELECT\n"
			   "    current_schema()" ) == "idhan" );
	tx.exec( "CREATE TABLE schema_probe (\n"
		"    id integer\n"
		")" );
	CHECK(
		tx.query_value< std::string >(
			"SELECT\n"
			"    n.nspname\n"
			"FROM pg_class c\n"
			"    JOIN pg_namespace n ON n.oid = c.relnamespace\n"
			"WHERE c.relname = 'schema_probe'" )
		== "idhan" );
	CHECK( tx.query_value< double >( "SELECT\n"
			   "    B'1010' <~> B'1000'" ) == 1.0 );
	CHECK( tx.query_value< double >( "SELECT\n"
			   "    '[1,0]'::halfvec <=> '[1,0]'::halfvec" ) == 0.0 );
	db::initializeDatabase( tx, "idhan" );
	CHECK( installedExtensions( tx ) == extensions );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Permission failures report every missing extension and preserve the parent transaction",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	const bool superuser {
		tx.query_value< bool >( "SELECT\n"
			"    rolsuper\n"
			"FROM pg_catalog.pg_roles\n"
			"WHERE rolname = current_user" )
	};
	if ( !superuser ) SKIP( "Permission-denial fixture requires SET ROLE privileges" );

	const auto extensions { installedExtensions( tx ) };
	tx.exec( "SET LOCAL ROLE pg_monitor" );
	for ( const auto name : { "pgcrypto", "pg_trgm", "pg_stat_statements", "vector" } )
	{
		CHECK_THROWS_WITH(
			db::initializeDatabase( tx, "idhan" ),
			Catch::Matchers::ContainsSubstring(
				"CREATE EXTENSION IF NOT EXISTS \"" + std::string { name } + "\" WITH SCHEMA \"public\";" ) );
	}
	CHECK_THROWS_WITH( db::initializeDatabase( tx, "idhan" ), Catch::Matchers::ContainsSubstring( "SQLSTATE 42501" ) );
	CHECK( installedExtensions( tx ) == extensions );
	CHECK_FALSE( tx.query_value< bool >( "SELECT\n"
		"    EXISTS (\n"
		"        SELECT\n"
		"            1\n"
		"        FROM pg_namespace\n"
		"        WHERE nspname = 'idhan'\n"
		"    )" ) );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Each missing extension is installed in public while preserving unrelated data",
	"[db][schema][bootstrap]" )
{
	const std::string missing { GENERATE( "pgcrypto", "pg_trgm", "pg_stat_statements", "vector" ) };
	pqxx::work tx { connection() };
	installTestExtensions( tx );
	tx.exec( "DROP EXTENSION " + tx.quote_name( missing ) );
	tx.exec(
		"CREATE SCHEMA other_app;\n"
		"\n"
		"CREATE TABLE other_app.records (\n"
		"    id integer\n"
		");\n"
		"\n"
		"INSERT INTO other_app.records\n"
		"    VALUES (42)" );
	const auto other_extensions { tx.query_value< std::string >(
		"SELECT string_agg(oid::text, ','\n"
		"                  ORDER BY extname)\n"
		"FROM pg_catalog.pg_extension\n"
		"WHERE extname <> $1",
		pqxx::params { missing } ) };
	db::initializeDatabase( tx, "idhan" );
	CHECK(
		tx.query_value< std::string >(
			"SELECT\n"
			"    n.nspname\n"
			"FROM pg_catalog.pg_extension e\n"
			"    JOIN pg_catalog.pg_namespace n ON n.oid = e.extnamespace\n"
			"WHERE e.extname = $1",
			pqxx::params { missing } )
		== "public" );
	CHECK(
		tx.query_value< std::string >(
			"SELECT string_agg(oid::text, ','\n"
			"                  ORDER BY extname)\n"
			"FROM pg_catalog.pg_extension\n"
			"WHERE extname <> $1",
			pqxx::params { missing } )
		== other_extensions );
	CHECK( tx.query_value< int >( "SELECT\n"
			   "    id\n"
			   "FROM other_app.records" ) == 42 );
	CHECK( tx.query_value< std::string >( "SELECT\n"
			   "    current_schema()" ) == "idhan" );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"An empty database installs required extensions and repeated initialization preserves them",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	requireAvailableExtensions( tx );
	db::initializeDatabase( tx, "idhan" );
	CHECK(
		tx.query_value< int >(
			"SELECT\n"
			"    count(*)\n"
			"FROM pg_catalog.pg_extension e\n"
			"    JOIN pg_catalog.pg_namespace n ON n.oid = e.extnamespace\n"
			"WHERE e.extname IN ('pgcrypto', 'pg_trgm', 'pg_stat_statements', 'vector')\n"
			"    AND n.nspname = 'public'" )
		== 4 );
	const auto extensions { installedExtensions( tx ) };
	db::initializeDatabase( tx, "idhan" );
	CHECK( installedExtensions( tx ) == extensions );
	CHECK( tx.query_value< std::string >( "SELECT\n"
			   "    current_schema()" ) == "idhan" );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Unavailable extension files roll back all bootstrap changes",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	const auto missing { tx.exec(
			"SELECT\n"
			"    name\n"
			"FROM (\n"
			"    VALUES ('pgcrypto'),\n"
			"        ('pg_trgm'),\n"
			"        ('pg_stat_statements'),\n"
			"        ('vector')\n"
			") required (name)\n"
			"WHERE NOT EXISTS (\n"
			"        SELECT\n"
			"            1\n"
			"        FROM pg_catalog.pg_available_extensions a\n"
			"        WHERE a.name = required.name\n"
			")" )
	};
	if ( missing.empty() ) SKIP( "All required extension files are available" );

	const auto extensions { installedExtensions( tx ) };
	for ( const auto& row : missing )
		CHECK_THROWS_WITH(
			db::initializeDatabase( tx, "idhan" ),
			Catch::Matchers::ContainsSubstring(
				"Could not install " + row[ 0 ].as< std::string >() + " in \"public\"" ) );
	CHECK( installedExtensions( tx ) == extensions );
	CHECK_FALSE( tx.query_value< bool >( "SELECT\n"
		"    EXISTS (\n"
		"        SELECT\n"
		"            1\n"
		"        FROM pg_namespace\n"
		"        WHERE nspname = 'idhan'\n"
		"    )" ) );
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
		"CREATE SCHEMA other_app;\n"
		"\n"
		"CREATE SCHEMA another_app;\n"
		"\n"
		"CREATE TABLE other_app.records (\n"
		"    id integer\n"
		");\n"
		"\n"
		"INSERT INTO other_app.records\n"
		"    VALUES (42);\n"
		"\n"
		"CREATE TABLE public.records (\n"
		"    id integer\n"
		");\n"
		"\n"
		"INSERT INTO public.records\n"
		"    VALUES (84);\n"
		"\n"
		"CREATE VIEW other_app.record_view AS\n"
		"SELECT\n"
		"    *\n"
		"FROM other_app.records;\n"
		"\n"
		"CREATE SEQUENCE public.user_sequence\n"
		"    START WITH 17" );
	const auto extensions { installedExtensions( tx ) };
	db::initializeDatabase( tx, "idhan" );
	tx.exec( "CREATE TABLE records (\n"
		"    id integer\n"
		");\n"
		"\n"
		"INSERT INTO records\n"
		"    VALUES (21)" );
	CHECK( tx.query_value< std::string >( "SELECT\n"
			   "    current_schema()" ) == "idhan" );
	CHECK( tx.query_value< int >( "SELECT\n"
			   "    id\n"
			   "FROM records" ) == 21 );
	CHECK( tx.query_value< int >( "SELECT\n"
			   "    id\n"
			   "FROM other_app.records" ) == 42 );
	CHECK( tx.query_value< int >( "SELECT\n"
			   "    id\n"
			   "FROM public.records" ) == 84 );
	CHECK( tx.query_value< int >( "SELECT\n"
			   "    id\n"
			   "FROM other_app.record_view" ) == 42 );
	CHECK( tx.query_value< int >( "SELECT\n"
			   "    nextval('public.user_sequence')" ) == 17 );
	db::initializeDatabase( tx, "idhan" );
	CHECK( installedExtensions( tx ) == extensions );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Public migration history always prevents application initialization",
	"[db][schema][bootstrap]" )
{
	const std::string schema { GENERATE( "idhan", "public", "custom_app" ) };
	const bool initialized { GENERATE( false, true ) };
	const std::string extension_schema { GENERATE( "public", "extensions" ) };
	pqxx::work tx { connection() };
	tx.exec(
		"CREATE TABLE public.idhan_info (\n"
		"    object_name text PRIMARY KEY\n"
		");\n"
		"\n"
		"INSERT INTO public.idhan_info\n"
		"    VALUES ('table_records');\n"
		"\n"
		"CREATE TABLE public.records (\n"
		"    id integer\n"
		");\n"
		"\n"
		"INSERT INTO public.records\n"
		"    VALUES (42)" );
	if ( initialized && schema != "public" )
	{
		tx.exec( "CREATE SCHEMA " + tx.quote_name( schema ) );
		tx.exec( "CREATE TABLE " + tx.quote_name( schema ) + ".idhan_info (object_name text PRIMARY KEY)" );
	}

	const auto extensions { installedExtensions( tx ) };
	const auto search_path { tx.query_value< std::string >( "SHOW search_path" ) };
	CHECK_THROWS_WITH(
		db::initializeDatabase( tx, schema, extension_schema ),
		Catch::Matchers::ContainsSubstring( "public.idhan_info exists" ) );
	CHECK_THROWS_WITH(
		db::initializeDatabase( tx, schema, extension_schema ),
		Catch::Matchers::ContainsSubstring( "Migrate the existing installation manually" ) );
	CHECK( tx.query_value< std::string >( "SHOW search_path" ) == search_path );
	CHECK( installedExtensions( tx ) == extensions );
	CHECK( tx.query_value< int >( "SELECT\n"
			   "    id\n"
			   "FROM public.records" ) == 42 );
	CHECK( tx.query_value< std::string >( "SELECT\n"
			   "    object_name\n"
			   "FROM public.idhan_info" ) == "table_records" );
	if ( schema != "public" )
		CHECK(
			tx.query_value< bool >(
				"SELECT\n"
				"    EXISTS (\n"
				"        SELECT\n"
				"            1\n"
				"        FROM pg_catalog.pg_namespace\n"
				"        WHERE nspname = $1\n"
				"    )",
				pqxx::params{ schema } )
			== initialized );
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
	CHECK( tx.query_value< std::string >( "SELECT\n"
			   "    current_schema()" ) == schema );
	CHECK( tx.query_value< double >( "SELECT\n"
			   "    B'1010' <~> B'1000'" ) == 1.0 );
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
	tx.exec( "CREATE SCHEMA extensions;\n"
			 "\n"
			 "ALTER EXTENSION " + tx.quote_name( extension ) + " SET SCHEMA extensions" );
	const auto extensions { installedExtensions( tx ) };
	CHECK_THROWS_WITH(
		db::initializeDatabase( tx, "idhan" ),
		Catch::Matchers::ContainsSubstring( "Extension " + extension + " is installed in" ) );
	CHECK( installedExtensions( tx ) == extensions );
	CHECK_FALSE( tx.query_value< bool >( "SELECT\n"
		"    EXISTS (\n"
		"        SELECT\n"
		"            1\n"
		"        FROM pg_namespace\n"
		"        WHERE nspname = 'idhan'\n"
		"    )" ) );
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
		"CREATE SCHEMA legacy_app;\n"
		"\n"
		"ALTER EXTENSION pgcrypto SET SCHEMA legacy_app;\n"
		"\n"
		"ALTER EXTENSION pg_trgm SET SCHEMA legacy_app;\n"
		"\n"
		"ALTER EXTENSION pg_stat_statements SET SCHEMA legacy_app" );
	const auto extensions { installedExtensions( tx ) };
	db::initializeDatabase( tx, "legacy_app" );
	CHECK( tx.query_value< std::string >( "SELECT\n"
			   "    current_schema()" ) == "legacy_app" );
	CHECK( tx.query_value< int >( "SELECT\n"
			   "    length(gen_random_bytes(32))" ) == 32 );
	CHECK( tx.query_value< double >( "SELECT\n"
			   "    similarity ('test', 'test')" ) == 1.0 );
	CHECK( installedExtensions( tx ) == extensions );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Custom extension schemas support installation and quoted identifiers",
	"[db][schema][bootstrap]" )
{
	const std::string extensions { GENERATE( "extensions", "Mixed,\"extensions", "idhan" ) };
	pqxx::work tx { connection() };
	requireAvailableExtensions( tx );
	db::initializeDatabase( tx, "idhan", extensions );
	CHECK(
		tx.query_value< int >(
			"SELECT\n"
			"    count(*)\n"
			"FROM pg_catalog.pg_extension e\n"
			"    JOIN pg_catalog.pg_namespace n ON n.oid = e.extnamespace\n"
			"WHERE e.extname IN ('pgcrypto', 'pg_trgm', 'pg_stat_statements', 'vector')\n"
			"    AND n.nspname = $1",
			pqxx::params { extensions } )
		== 4 );
	CHECK( tx.query_value< std::string >( "SHOW search_path" ) == db::makeSearchPath( "idhan", extensions ) );
	CHECK( tx.query_value< double >( "SELECT\n"
			   "    B'1010' <~> B'1000'" ) == 1.0 );
	CHECK( tx.query_value< double >( "SELECT\n"
			   "    '[1,0]'::halfvec <=> '[1,0]'::halfvec" ) == 0.0 );
	const auto installed { installedExtensions( tx ) };
	db::initializeDatabase( tx, "idhan", extensions );
	CHECK( installedExtensions( tx ) == installed );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Extensions in a different schema require explicit configuration and are not moved",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	tx.exec( "CREATE EXTENSION pgcrypto WITH SCHEMA public" );
	const auto installed { installedExtensions( tx ) };
	CHECK_THROWS_WITH(
		db::initializeDatabase( tx, "idhan", "extensions" ),
		Catch::Matchers::ContainsSubstring(
			"Extension pgcrypto is installed in \"public\"; it must be available in \"extensions\"" ) );
	CHECK( installedExtensions( tx ) == installed );
	CHECK_FALSE( tx.query_value< bool >(
		"SELECT\n"
		"    EXISTS (\n"
		"        SELECT\n"
		"            1\n"
		"        FROM pg_namespace\n"
		"        WHERE nspname IN ('idhan', 'extensions')\n"
		"    )" ) );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Failure to install extensions rolls back a newly created custom schema",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	const auto missing { tx.exec(
			"SELECT\n"
			"    name\n"
			"FROM (\n"
			"    VALUES ('pgcrypto'),\n"
			"        ('pg_trgm'),\n"
			"        ('pg_stat_statements'),\n"
			"        ('vector')\n"
			") required (name)\n"
			"WHERE NOT EXISTS (\n"
			"        SELECT\n"
			"            1\n"
			"        FROM pg_catalog.pg_available_extensions a\n"
			"        WHERE a.name = required.name\n"
			")" )
	};
	if ( missing.empty() ) SKIP( "All required extension files are available" );

	const auto installed { installedExtensions( tx ) };
	CHECK_THROWS_WITH(
		db::initializeDatabase( tx, "idhan", "Mixed,\"extensions" ),
		Catch::Matchers::ContainsSubstring( "WITH SCHEMA \"Mixed,\"\"extensions\";" ) );
	CHECK( installedExtensions( tx ) == installed );
	CHECK_FALSE( tx.query_value< bool >(
		"SELECT\n"
		"    EXISTS (\n"
		"        SELECT\n"
		"            1\n"
		"        FROM pg_namespace\n"
		"        WHERE nspname = $1\n"
		"    )",
		pqxx::params{ "Mixed,\"extensions" } ) );
	tx.commit();
}

TEST_CASE_METHOD(
	InitializationDatabase,
	"Creating an extension namespace requires database privileges",
	"[db][schema][bootstrap]" )
{
	pqxx::work tx { connection() };
	const bool superuser {
		tx.query_value< bool >( "SELECT\n"
			"    rolsuper\n"
			"FROM pg_catalog.pg_roles\n"
			"WHERE rolname = current_user" )
	};
	if ( !superuser ) SKIP( "Permission-denial fixture requires SET ROLE privileges" );

	const auto installed { installedExtensions( tx ) };
	tx.exec( "SET LOCAL ROLE pg_monitor" );
	CHECK_THROWS_AS( db::initializeDatabase( tx, "idhan", "extensions" ), pqxx::insufficient_privilege );
	CHECK( installedExtensions( tx ) == installed );
	CHECK_FALSE( tx.query_value< bool >(
		"SELECT\n"
		"    EXISTS (\n"
		"        SELECT\n"
		"            1\n"
		"        FROM pg_namespace\n"
		"        WHERE nspname IN ('idhan', 'extensions')\n"
		"    )" ) );
	tx.commit();
}

TEST_CASE( "Database configuration validates the extension schema identifier", "[db][schema]" )
{
	db::DBConfig config;
	config.extension_schema = "Mixed,\"extensions";
	CHECK_NOTHROW( config.validate() );
	config.extension_schema.clear();
	CHECK_THROWS_AS( config.validate(), std::invalid_argument );
	config.extension_schema = std::string { "a\0b", 3 };
	CHECK_THROWS_AS( config.validate(), std::invalid_argument );
}

} // namespace idhan::test
