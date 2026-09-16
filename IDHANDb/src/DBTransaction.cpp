#include "idhan/db/DBTransaction.hpp"

#include <folly/Try.h>
#include <folly/coro/Task.h>

#include <mutex>
#include <stdexcept>
#include <utility>

#include "IDHANDB.hpp"
#include "idhan/db/DBConnection.hpp"
#include "idhan/db/DBOperation.hpp"

namespace idhan::db
{
struct DBTransaction::State
{
	std::mutex mutex;
	std::shared_ptr< DBConnection > connection;
	folly::Executor::KeepAlive<> application_executor;
	std::function< void( std::shared_ptr< DBConnection > ) > release;
	TransactionPhase current_phase { TransactionPhase::active };
};

static DBTask createTask(
	const std::shared_ptr< DBTransaction::State >& state,
	std::string sql,
	DBBindings bindings = {},
	QueryOptions options = {} )
{
	auto operation {
		std::make_shared< DBOperation >( std::move( sql ), std::move( bindings ), options, state->application_executor )
	};
	auto connection { state->connection };
	operation->setStarter( [ connection = std::move( connection ) ]( std::shared_ptr< DBOperation > queued )
	                       { connection->enqueue( std::move( queued ) ); } );
	return DBTask { std::move( operation ) };
}

static void releaseLease( const std::shared_ptr< DBTransaction::State >& state )
{
	std::shared_ptr< DBConnection > connection;
	std::function< void( std::shared_ptr< DBConnection > ) > release;
	{
		std::lock_guard lock( state->mutex );
		if ( state->current_phase == TransactionPhase::completed ) return;
		state->current_phase = TransactionPhase::completed;
		connection = std::move( state->connection );
		release = std::move( state->release );
	}
	if ( connection && release ) release( std::move( connection ) );
}

static runtime::Task<> finalize( std::shared_ptr< DBTransaction::State > state, std::string command )
{
	try
	{
		co_await createTask( state, std::move( command ) ).result();
	}
	catch ( ... )
	{
		releaseLease( state );
		throw;
	}
	releaseLease( state );
}

static void queueRollback( const std::shared_ptr< DBTransaction::State >& state )
{
	if ( !state ) return;

	bool rollback { false };
	{
		std::lock_guard lock( state->mutex );
		if ( state->current_phase == TransactionPhase::active )
		{
			state->current_phase = TransactionPhase::finalizing;
			rollback = true;
		}
	}
	if ( !rollback ) return;

	folly::coro::co_withExecutor( state->application_executor, finalize( state, "ROLLBACK" ) )
		.start( []( folly::Try< void >&& ) {} );
}

DBTransaction::DBTransaction( std::shared_ptr< State > state ) : m_state( std::move( state ) )
{}

DBTransaction DBTransaction::create(
	std::shared_ptr< DBConnection > connection,
	folly::Executor::KeepAlive<> application_executor,
	std::function< void( std::shared_ptr< DBConnection > ) > release )
{
	if ( !connection || !application_executor || !release )
		throw std::invalid_argument(
			"Database transaction requires a connection, executor, and lease release callback" );

	auto state { std::make_shared< State >() };
	state->connection = std::move( connection );
	state->application_executor = std::move( application_executor );
	state->release = std::move( release );
	return DBTransaction { std::move( state ) };
}

DBTransaction::DBTransaction( DBTransaction&& ) noexcept = default;

DBTransaction& DBTransaction::operator=( DBTransaction&& other ) noexcept
{
	if ( this == &other ) return *this;
	queueRollback( m_state );
	m_state = std::move( other.m_state );
	return *this;
}

DBTransaction::~DBTransaction()
{
	queueRollback( m_state );
}

DBTask DBTransaction::query( std::string sql, DBBindings bindings, QueryOptions options )
{
	if ( !m_state ) throw std::logic_error( "Cannot query a moved-from database transaction" );
	std::lock_guard lock( m_state->mutex );
	if ( m_state->current_phase != TransactionPhase::active )
		throw std::logic_error( "Cannot query a database transaction that is not active" );
	return createTask( m_state, std::move( sql ), std::move( bindings ), options );
}

runtime::Task<> DBTransaction::commit()
{
	if ( !m_state ) throw std::logic_error( "Cannot commit a moved-from database transaction" );
	{
		std::lock_guard lock( m_state->mutex );
		if ( m_state->current_phase != TransactionPhase::active )
			throw std::logic_error( "Cannot commit a database transaction that is not active" );
		m_state->current_phase = TransactionPhase::finalizing;
	}
	co_await finalize( m_state, "COMMIT" );
}

runtime::Task<> DBTransaction::rollback()
{
	if ( !m_state ) throw std::logic_error( "Cannot roll back a moved-from database transaction" );
	{
		std::lock_guard lock( m_state->mutex );
		if ( m_state->current_phase != TransactionPhase::active )
			throw std::logic_error( "Cannot roll back a database transaction that is not active" );
		m_state->current_phase = TransactionPhase::finalizing;
	}
	co_await finalize( m_state, "ROLLBACK" );
}

TransactionPhase DBTransaction::phase() const noexcept
{
	if ( !m_state ) return TransactionPhase::completed;
	std::lock_guard lock( m_state->mutex );
	return m_state->current_phase;
}
} // namespace idhan::db
