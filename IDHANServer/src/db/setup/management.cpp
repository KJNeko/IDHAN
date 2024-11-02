//
// Created by kj16609 on 9/8/24.
//

#include "management.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#include <pqxx/nontransaction>
#include <pqxx/result>
#pragma GCC diagnostic pop

#include "logging/log.hpp"

namespace idhan::db
{

	bool tableExists( pqxx::nontransaction& tx, const std::string_view name )
	{
		const pqxx::result table_result {
			tx.exec_params( "SELECT table_name FROM information_schema.tables WHERE table_name = $1", name )
		};

		return table_result.size() > 0;
	}

	//! Returns the table version.
	std::uint16_t getTableVersion( pqxx::nontransaction& tx, const std::string_view name )
	{
		auto result { tx.exec_params( "SELECT table_version FROM idhan_info WHERE table_name = $1", name ) };

		if ( result.size() == 0 ) return 0;

		return std::get< 0 >( result.at( 0 ).as< std::uint16_t >() );
	}

	void addTableToInfo( pqxx::nontransaction& tx, const std::string_view name, const std::string_view creation_query )
	{
		tx.exec_params(
			"INSERT INTO idhan_info ( table_version, table_name, creation_query ) VALUES ($1, $2, $3)",
			1,
			name,
			creation_query );
	}

	void updateTableVersion( pqxx::nontransaction& tx, const std::string_view name, const std::uint16_t version )
	{
		tx.exec_params( "UPDATE idhan_info SET table_version = $1 WHERE table_name = $2", version, name );
	}

#ifdef ALLOW_TABLE_DESTRUCTION

	void destroyTables( pqxx::nontransaction& tx )
	{
		log::critical(
			"We are about to drop the public schema since we are compiling with ALLOW_TABLE_DESTRUCTION! This will happen in 5 seconds. QUIT NOW IF YOU DON'T WANT THIS TO HAPPEN" );
		std::this_thread::sleep_for( std::chrono::seconds( 5 ) );

		if ( !tx.exec( "SELECT schema_name FROM information_schema.schemata WHERE schema_name = \'public\'" ).empty() )
		{
			tx.exec( "DROP SCHEMA public CASCADE" );
		}
		else
		{
			log::debug( "Public schema does not exist. Skipping drop." );
		}

		tx.exec( "CREATE SCHEMA public" );
	}

#endif

} // namespace idhan::db
