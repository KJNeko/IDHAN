#pragma once
#include <libpq-fe.h>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBConnection;

class PreparedStatementCache
{
	DBConnection& m_connection;
	std::mutex m_mutex;
	std::unordered_map< std::string, std::string > m_statement_names;

  public:

	explicit PreparedStatementCache( DBConnection& );
	~PreparedStatementCache();
	[[nodiscard]] runtime::Task< std::string > prepare( std::string sql, std::vector< Oid > parameter_oids );
	void invalidate( std::string_view statement );
	[[nodiscard]] runtime::Task<> clear();
};
} // namespace idhan::db
