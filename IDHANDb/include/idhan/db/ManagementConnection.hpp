#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "DBBindings.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBTask;
class DBReactor;
class DBConnection;

struct CommandSummary
{
	std::string tag;
	std::string affected_rows;
};

class ManagementConnection
{
	DBConfig m_config;
	DBReactor& m_reactor;
	std::shared_ptr< DBConnection > m_connection;
	std::mutex m_mutex;
	bool m_started { false };
	bool m_leased { false };

  public:

	class Lease
	{
		std::shared_ptr< DBConnection > m_connection;
		std::function< void() > m_release;
		friend class ManagementConnection;
		explicit Lease( std::shared_ptr< DBConnection >, std::function< void() > );

	  public:

		Lease( Lease&& ) noexcept;
		Lease& operator=( Lease&& ) noexcept;
		Lease( const Lease& ) = delete;
		Lease& operator=( const Lease& ) = delete;
		~Lease();
		[[nodiscard]] DBTask query( std::string, DBBindings = {}, QueryOptions = {} );
		// Trusted scripts only; drain every simple-protocol result.
		[[nodiscard]] runtime::Task< std::vector< CommandSummary > > executeScript( std::string );
		[[nodiscard]] runtime::Task<> begin();
		[[nodiscard]] runtime::Task<> commit();
		[[nodiscard]] runtime::Task<> rollback();
	};

	ManagementConnection( DBConfig, DBReactor& );
	~ManagementConnection();
	[[nodiscard]] runtime::Task<> start();
	[[nodiscard]] runtime::Task< Lease > acquire();
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::db
