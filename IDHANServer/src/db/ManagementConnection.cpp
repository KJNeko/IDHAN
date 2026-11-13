#include "ManagementConnection.hpp"

#include "ConnectionArguments.hpp"
#include "initialize.hpp"
#include "logging/log.hpp"
#include "management.hpp"
#include "migrations.hpp"

namespace idhan
{

void checkPgVersion( const std::string& pg_version )
{
	if ( pg_version.empty() )
	{
		log::critical( "Postgres version string was missing" );
		std::abort();
	}

	const auto pre_version { pg_version.find_first_of( ' ' ) };
	if ( pre_version == std::string::npos )
	{
		log::critical( "Failed to parse pg version string" );
		std::abort();
	}

	const auto post_version { pg_version.find_first_of( ' ', pre_version + 1 ) };

	if ( post_version == std::string::npos )
	{
		log::critical( "Failed to parse postgres version string" );
		std::abort();
	}

	const auto divider { pg_version.find_first_of( '.', pre_version + 1 ) };

	if ( divider == std::string::npos )
	{
		log::critical( "Failed to parse postgres version string" );
		std::abort();
	}

	const auto major_str { pg_version.substr( pre_version, divider ) };

	std::istringstream ss { major_str };
	std::size_t major { 0 };
	ss >> major;
	if ( ss.fail() )
	{
		log::critical( "Failed to parse postgres version string" );
		std::abort();
	}

	if ( major < 18 )
	{
		log::critical( "Minimum of postgres 18 required for IDHAN" );
		std::abort();
	}
}

ManagementConnection::ManagementConnection( const ConnectionArguments& arguments ) : connection( arguments.format() )
{
	log::info( "Postgres management connection made" );

	log::info( "Interrogating postgres version" );
	{
		pqxx::work tx { connection };
		const auto version{
			tx.exec(
				//language=postgresql
				R"SQL(
				SELECT
				    version()
			)SQL" )
		};
		const auto version_string { version[ 0 ][ 0 ].as< std::string >() };
		log::info( "Postgres version: {}", version_string );
		checkPgVersion( version_string );
		db::initializeDatabase( tx, arguments.schema, arguments.extension_schema );
		tx.commit();
	}

	pqxx::nontransaction tx { connection };
	db::updateMigrations( tx, arguments.schema );

	log::info( "Database loading finished" );
}

std::string ConnectionArguments::format() const
{
	std::string str;
	if ( hostname.empty() ) throw std::runtime_error( "Hostname empty" );

	if ( port == std::numeric_limits< std::uint16_t >::quiet_NaN() ) throw std::runtime_error( "Port not set" );

	str += format_ns::format( "host={} ", hostname );
	str += format_ns::format( "port={} ", port );
	str += format_ns::format( "dbname={} ", dbname );
	str += format_ns::format( "user={} ", user );
	if ( !password.empty() ) str += format_ns::format( "password={} ", password );
	if ( log_level <= spdlog::level::debug ) str += "options='-c client_min_messages=debug1'";

	return str;
}
} // namespace idhan
