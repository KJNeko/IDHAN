#pragma once
#include <folly/Executor.h>

#include <memory>
#include <string>

#include "DBPool.hpp"
#include "DBTransaction.hpp"
#include "IDHANDB.hpp"
#include "ManagementConnection.hpp"

namespace idhan::db
{

class DBContext
{
	DBConfig m_config;
	folly::Executor::KeepAlive<> m_application_executor;
	std::shared_ptr< DBReactor > m_reactor;
	std::shared_ptr< DBPool > m_pool;

	static inline DBContext* s_instance { nullptr };

  public:

	DBContext( DBConfig, folly::Executor::KeepAlive<> application_executor );
	~DBContext();
	DBContext( const DBContext& ) = delete;
	DBContext& operator=( const DBContext& ) = delete;

	//! The process constructs exactly one DBContext (ServerContext, at startup). Code that would
	//! otherwise need it threaded through as an explicit parameter (e.g. singletons such as
	//! DownloadSessionManager) can acquire it here instead.
	[[nodiscard]] static DBContext& instance();

	[[nodiscard]] runtime::Task<> start();
	[[nodiscard]] DBTask query( std::string sql, DBBindings = {}, QueryOptions = {} );
	[[nodiscard]] runtime::Task< DBTransaction > transaction();
	[[nodiscard]] std::vector< ConnectionStats > stats() const;
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::db
