//
// Created by kj16609 on 6/23/23.
//

#include "Binder.hpp"

#include "idhan/logging/logger.hpp"

namespace idhan::hydrus
{
	Binder::Binder( sqlite3* db, const std::string_view sql ) : ptr( db )
	{
		const char* unused { nullptr };
		const auto prepare_ret {
			sqlite3_prepare_v2( ptr, sql.data(), static_cast< int >( sql.size() + 1 ), &stmt, &unused )
		};

		if ( unused != nullptr && strlen( unused ) > 0 )
		{
			//Check if the string is just empty (\n or \t)
			const std::string_view leftovers { unused };
			auto itter { leftovers.begin() };
			while ( itter != leftovers.end() )
			{
				if ( *itter == '\n' || *itter == '\t' )
				{
					++itter;
					continue;
				}
				else
					throw std::runtime_error(
						std::format( "Query had unused portions of the input. Unused: \"{}\"", unused ) );
			}
		}

		if ( stmt == nullptr )
			throw std::runtime_error( std::format( "Failed to prepare stmt, {}", sqlite3_errmsg( ptr ) ) );

		if ( prepare_ret != SQLITE_OK )
		{
			throw std::runtime_error(
				std::format( "DB: Failed to prepare statement: \"{}\", Reason: \"{}\"", sql, sqlite3_errmsg( ptr ) ) );
		}

		max_param_count = sqlite3_bind_parameter_count( stmt );
	}

	Binder::~Binder()
	{
		try
		{
			if ( !ran )
			{
				std::optional< std::tuple<> > tpl;
				executeQuery( tpl );
			}

			sqlite3_finalize( stmt );
		}
		catch ( std::exception& e )
		{
			IDHAN::log::critical( "Binder's dtor has thrown!, {}", e.what() );
		}
		catch ( ... )
		{
			IDHAN::log::critical( "Binder's dtor has thrown!, ..." );
		}
	}
} // namespace idhan::hydrus