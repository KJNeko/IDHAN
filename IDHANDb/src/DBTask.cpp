#include <stdexcept>
#include <utility>

#include "IDHANDB.hpp"
#include "idhan/db/DBOperation.hpp"

namespace idhan::db
{
DBTask::DBTask( std::shared_ptr< DBOperation > operation ) : m_operation( std::move( operation ) )
{
	if ( !m_operation ) throw std::invalid_argument( "DBTask requires an operation" );
}

DBTask::Awaiter::Awaiter( std::shared_ptr< DBOperation > operation ) : m_operation( std::move( operation ) )
{}

DBTask::Awaiter::~Awaiter()
{
	if ( m_operation )
	{
		m_operation->unregisterContinuation();
		m_operation->abandon();
	}
}

DBTask::Awaiter::Awaiter( Awaiter&& ) noexcept = default;
DBTask::Awaiter& DBTask::Awaiter::operator=( Awaiter&& ) noexcept = default;

bool DBTask::Awaiter::await_ready() const noexcept
{
	return m_operation->completed();
}

void DBTask::Awaiter::await_suspend( const std::coroutine_handle<> continuation )
{
	m_operation->start();
	m_operation->registerContinuation( continuation );
}

DBResult DBTask::Awaiter::await_resume()
{
	m_operation->rethrowIfFailed();
	return m_operation->takeResult();
}

DBTask::Awaiter DBTask::operator co_await() &
{
	return Awaiter { m_operation };
}

DBTask::Awaiter DBTask::operator co_await() &&
{
	return Awaiter { std::move( m_operation ) };
}

runtime::Task< DBResult > DBTask::result()
{
	co_return co_await *this;
}

void DBTask::abandon() noexcept
{
	if ( m_operation ) m_operation->abandon();
}
} // namespace idhan::db
