#pragma once
#include <folly/Executor.h>

#include <functional>
#include <memory>
#include <string>

#include "DBBindings.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBTask;
class DBPool;
class DBConnection;
enum class TransactionPhase
{
	acquiring,
	active,
	finalizing,
	completed
};

class DBTransaction
{
  public:

	struct State;

  private:

	std::shared_ptr< State > m_state;
	friend class DBPool;
	explicit DBTransaction( std::shared_ptr< State > );
	[[nodiscard]] static DBTransaction create(
		std::shared_ptr< DBConnection >,
		folly::Executor::KeepAlive<>,
		std::function< void( std::shared_ptr< DBConnection > ) > );

  public:

	DBTransaction( DBTransaction&& ) noexcept;
	DBTransaction& operator=( DBTransaction&& ) noexcept;
	DBTransaction( const DBTransaction& ) = delete;
	DBTransaction& operator=( const DBTransaction& ) = delete;
	// Queues rollback; the connection lease survives this object.
	~DBTransaction();
	[[nodiscard]] DBTask query( std::string sql, DBBindings = {}, QueryOptions = {} );
	[[nodiscard]] runtime::Task<> commit();
	[[nodiscard]] runtime::Task<> rollback();
	[[nodiscard]] TransactionPhase phase() const noexcept;
};
} // namespace idhan::db
