#include "idhan/db/DBOperation.hpp"

#include <atomic>
#include <stdexcept>
#include <utility>

#include "idhan/db/DBError.hpp"

namespace idhan::db
{
static std::atomic< std::uint64_t > next_operation_id { 1 };

static void resumeOnApplicationExecutor( folly::Executor::KeepAlive<> executor, std::coroutine_handle<> continuation )
{
	if ( continuation ) executor->add( [ continuation ] { continuation.resume(); } );
}

DBOperation::DBOperation(
	std::string sql,
	DBBindings bindings,
	QueryOptions options,
	folly::Executor::KeepAlive<> application_executor ) :
  m_id( next_operation_id.fetch_add( 1, std::memory_order_relaxed ) ),
  m_sql( std::move( sql ) ),
  m_bindings( std::move( bindings ) ),
  m_options( options ),
  m_application_executor( std::move( application_executor ) )
{
	if ( m_sql.empty() ) throw std::invalid_argument( "Database SQL cannot be empty" );
}

DBOperation::~DBOperation() = default;

OperationPhase DBOperation::phase() const noexcept
{
	std::lock_guard lock( m_mutex );
	return m_phase;
}

std::uint64_t DBOperation::id() const noexcept
{
	return m_id;
}

void DBOperation::setStarter( std::function< void( std::shared_ptr< DBOperation > ) > starter )
{
	if ( !starter ) throw std::invalid_argument( "Database operation starter cannot be empty" );
	std::lock_guard lock( m_mutex );
	if ( m_phase != OperationPhase::cold ) throw std::logic_error( "Cannot alter a started database operation" );
	m_starter = std::move( starter );
}

void DBOperation::start()
{
	std::function< void( std::shared_ptr< DBOperation > ) > starter;
	{
		std::lock_guard lock( m_mutex );
		if ( m_phase != OperationPhase::cold ) return;
		if ( !m_starter ) throw std::logic_error( "Cold database operation has no pool starter" );
		m_phase = OperationPhase::queued;
		starter = m_starter;
	}
	starter( shared_from_this() );
}

void DBOperation::markQueued()
{
	std::lock_guard lock( m_mutex );
	if ( m_phase == OperationPhase::queued ) return;
	if ( m_phase != OperationPhase::cold ) throw std::logic_error( "Database operation was queued more than once" );
	m_phase = OperationPhase::queued;
}

bool DBOperation::send( PGconn* connection, const bool pipeline )
{
	const auto oids { m_bindings.oids() };
	const auto values { m_bindings.pointers() };
	const auto lengths { m_bindings.lengths() };
	const auto formats { m_bindings.formats() };
	if ( PQsendQueryParams(
			 connection,
			 m_sql.c_str(),
			 static_cast< int >( oids.size() ),
			 oids.data(),
			 values.data(),
			 lengths.data(),
			 formats.data(),
			 static_cast< int >( m_options.result_format ) )
	     != 1 )
		return false;
	if ( pipeline && PQpipelineSync( connection ) != 1 ) return false;

	std::lock_guard lock( m_mutex );
	if ( m_phase != OperationPhase::queued ) throw std::logic_error( "Sent database operation was not queued" );
	m_phase = OperationPhase::sent;
	m_sent_at = std::chrono::steady_clock::now();
	return true;
}

void DBOperation::acceptResult( DBResult result )
{
	std::lock_guard lock( m_mutex );
	if ( m_phase != OperationPhase::sent && m_phase != OperationPhase::synchronizing )
		throw std::logic_error( "Result does not belong to a sent database operation" );
	if ( m_result ) throw std::logic_error( "Database operation returned more than one primary result" );
	m_result = std::move( result );
	m_first_result_at = std::chrono::steady_clock::now();
	m_phase = OperationPhase::synchronizing;
}

void DBOperation::acceptFailure( std::exception_ptr error )
{
	if ( !error ) throw std::invalid_argument( "Database operation failure requires an exception" );
	std::lock_guard lock( m_mutex );
	if ( m_phase != OperationPhase::sent && m_phase != OperationPhase::synchronizing )
		throw std::logic_error( "Error does not belong to a sent database operation" );
	m_error = std::move( error );
	m_first_result_at = std::chrono::steady_clock::now();
	m_phase = OperationPhase::synchronizing;
}

void DBOperation::complete()
{
	std::coroutine_handle<> continuation;
	{
		std::lock_guard lock( m_mutex );
		if ( m_phase != OperationPhase::sent && m_phase != OperationPhase::synchronizing )
			throw std::logic_error( "Completed database operation was not sent" );
		m_phase = OperationPhase::completed;
		m_completed_at = std::chrono::steady_clock::now();
		continuation = std::exchange( m_continuation, {} );
	}
	resumeOnApplicationExecutor( m_application_executor, continuation );
}

void DBOperation::fail( std::exception_ptr error )
{
	if ( !error ) throw std::invalid_argument( "Database operation failure requires an exception" );
	std::coroutine_handle<> continuation;
	{
		std::lock_guard lock( m_mutex );
		if ( m_phase == OperationPhase::completed || m_phase == OperationPhase::abandoned )
			throw std::logic_error( "Cannot fail a completed database operation" );
		m_error = std::move( error );
		m_phase = OperationPhase::completed;
		continuation = std::exchange( m_continuation, {} );
	}
	resumeOnApplicationExecutor( m_application_executor, continuation );
}

DBResult DBOperation::takeResult()
{
	std::lock_guard lock( m_mutex );
	if ( m_consumer_cancelled ) throw DBOperationCancelled {};
	if ( m_phase != OperationPhase::completed ) throw std::logic_error( "Database result is not complete" );
	if ( m_error ) std::rethrow_exception( m_error );
	if ( !m_result ) throw std::logic_error( "Database operation completed without a result" );
	return std::move( m_result );
}

void DBOperation::rethrowIfFailed() const
{
	std::lock_guard lock( m_mutex );
	if ( m_consumer_cancelled ) throw DBOperationCancelled {};
	if ( m_phase != OperationPhase::completed ) throw std::logic_error( "Database operation is not complete" );
	if ( m_error ) std::rethrow_exception( m_error );
}

bool DBOperation::completed() const noexcept
{
	std::lock_guard lock( m_mutex );
	return m_phase == OperationPhase::completed || m_consumer_cancelled;
}

bool DBOperation::isSent() const noexcept
{
	std::lock_guard lock( m_mutex );
	return m_phase == OperationPhase::sent || m_phase == OperationPhase::synchronizing;
}

bool DBOperation::needsSynchronization() const noexcept
{
	std::lock_guard lock( m_mutex );
	return m_phase == OperationPhase::synchronizing;
}

const DBBindings& DBOperation::bindings() const noexcept
{
	return m_bindings;
}

const QueryOptions& DBOperation::options() const noexcept
{
	return m_options;
}

const std::string& DBOperation::sql() const noexcept
{
	return m_sql;
}

std::optional< std::chrono::milliseconds > DBOperation::firstResultLatency() const
{
	std::lock_guard lock( m_mutex );
	if ( !m_sent_at || !m_first_result_at ) return std::nullopt;
	return std::chrono::duration_cast< std::chrono::milliseconds >( *m_first_result_at - *m_sent_at );
}

std::optional< std::chrono::milliseconds > DBOperation::completionLatency() const
{
	std::lock_guard lock( m_mutex );
	if ( !m_sent_at || !m_completed_at ) return std::nullopt;
	return std::chrono::duration_cast< std::chrono::milliseconds >( *m_completed_at - *m_sent_at );
}

void DBOperation::abandon() noexcept
{
	std::coroutine_handle<> continuation;
	{
		std::lock_guard lock( m_mutex );
		if ( m_phase == OperationPhase::completed || m_phase == OperationPhase::abandoned ) return;
		if ( m_phase == OperationPhase::cold || m_phase == OperationPhase::queued )
		{
			m_error = std::make_exception_ptr( DBOperationCancelled {} );
			m_phase = OperationPhase::completed;
		}
		else
			m_consumer_cancelled = true;
		continuation = std::exchange( m_continuation, {} );
	}
	resumeOnApplicationExecutor( m_application_executor, continuation );
}

void DBOperation::consumerAdvanced()
{}

void DBOperation::registerContinuation( const std::coroutine_handle<> continuation )
{
	bool complete { false };
	{
		std::lock_guard lock( m_mutex );
		if ( m_continuation ) throw std::logic_error( "Database operation already has a consumer" );
		if ( m_phase == OperationPhase::completed || m_consumer_cancelled )
			complete = true;
		else
			m_continuation = continuation;
	}
	if ( complete ) resumeOnApplicationExecutor( m_application_executor, continuation );
}

void DBOperation::unregisterContinuation() noexcept
{
	std::lock_guard lock( m_mutex );
	m_continuation = {};
}
} // namespace idhan::db
