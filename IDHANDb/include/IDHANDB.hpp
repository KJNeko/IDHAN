#pragma once

#include <coroutine>
#include <memory>
#include <string>
#include <utility>

#include "idhan/db/DBResult.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBOperation;

class DBTask
{
	std::shared_ptr< DBOperation > m_operation {};

	friend class DBContext;
	friend class DBTransaction;

  public:

	explicit DBTask( std::shared_ptr< DBOperation > );

	class Awaiter
	{
		std::shared_ptr< DBOperation > m_operation {};
		friend class DBTask;
		explicit Awaiter( std::shared_ptr< DBOperation > );

	  public:

		~Awaiter();
		Awaiter( Awaiter&& ) noexcept;
		Awaiter& operator=( Awaiter&& ) noexcept;
		Awaiter( const Awaiter& ) = delete;
		Awaiter& operator=( const Awaiter& ) = delete;
		[[nodiscard]] bool await_ready() const noexcept;
		void await_suspend( std::coroutine_handle<> );
		[[nodiscard]] DBResult await_resume();
	};

	[[nodiscard]] Awaiter operator co_await() &;
	[[nodiscard]] Awaiter operator co_await() &&;
	[[nodiscard]] runtime::Task< DBResult > result();
	template < typename... Ts >
	[[nodiscard]] runtime::Task< DBRows< Ts... > > rows();
	void abandon() noexcept;
	[[nodiscard]] std::string querySql() const;

	DBTask() = delete;
	DBTask( const DBTask& ) = delete;
	DBTask& operator=( const DBTask& ) = delete;
	DBTask( DBTask&& ) noexcept = default;
	DBTask& operator=( DBTask&& ) noexcept = default;
};

template < typename... Ts >
runtime::Task< DBRows< Ts... > > DBTask::rows()
{
	const auto sql { querySql() };
	auto raw { co_await *this };
	co_return DBRows< Ts... >( std::move( raw ), sql );
}
} // namespace idhan::db
