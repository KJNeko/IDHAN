#include "idhan/db/DBConnection.hpp"

#include <folly/coro/Baton.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "idhan/db/DBError.hpp"
#include "idhan/db/DBOperation.hpp"
#include "idhan/db/DBReactor.hpp"

namespace idhan::db
{

[[nodiscard]] std::runtime_error connectionError( PGconn* connection, const char* operation )
{
	const char* detail { connection ? PQerrorMessage( connection ) : "libpq could not allocate a connection" };
	return std::runtime_error( std::format( "{}: {}", operation, detail ) );
}

[[nodiscard]] std::string diagnosticField( const PGresult* result, const int field )
{
	const auto* value { PQresultErrorField( result, field ) };
	return value ? value : "";
}

[[nodiscard]] std::exception_ptr queryError( PGresult* result, const std::uint64_t query_id )
{
	DBDiagnostics diagnostics {
		.sqlstate = diagnosticField( result, PG_DIAG_SQLSTATE ),
		.severity = diagnosticField( result, PG_DIAG_SEVERITY_NONLOCALIZED ),
		.message = diagnosticField( result, PG_DIAG_MESSAGE_PRIMARY ),
		.detail = diagnosticField( result, PG_DIAG_MESSAGE_DETAIL ),
		.hint = diagnosticField( result, PG_DIAG_MESSAGE_HINT ),
		.context = diagnosticField( result, PG_DIAG_CONTEXT ),
		.schema = diagnosticField( result, PG_DIAG_SCHEMA_NAME ),
		.table = diagnosticField( result, PG_DIAG_TABLE_NAME ),
		.column = diagnosticField( result, PG_DIAG_COLUMN_NAME ),
		.data_type = diagnosticField( result, PG_DIAG_DATATYPE_NAME ),
		.constraint = diagnosticField( result, PG_DIAG_CONSTRAINT_NAME ),
		.query_id = query_id,
	};

	if ( diagnostics.message.empty() ) diagnostics.message = PQresultErrorMessage( result );

	if ( diagnostics.sqlstate == "57014" /* query timeout state */ )
		return std::make_exception_ptr( DBQueryTimeout( std::move( diagnostics ) ) );

	if ( diagnostics.sqlstate.starts_with( "23" /* integrity constraint violation group */ ) )
		return std::make_exception_ptr( DBConstraintError( std::move( diagnostics ) ) );

	return std::make_exception_ptr( DBQueryError( std::move( diagnostics ) ) );
}

std::shared_ptr< DBOperation > DBConnection::getHead() const
{
	std::lock_guard lock { queue_mutex };
	if ( !inflight.empty() ) return inflight.front();
	return nullptr;
}

void DBConnection::pushInflight( std::shared_ptr< DBOperation > operation )
{
	std::lock_guard lock { queue_mutex };
	inflight.push_back( std::move( operation ) );
	++sent;
}

void DBConnection::retireHead()
{
	{
		std::lock_guard lock { queue_mutex };
		if ( !inflight.empty() )
		{
			inflight.pop_front();
			--sent;
		}
	}
	postDrainedIfIdle();
}

void DBConnection::postDrainedIfIdle()
{
	std::lock_guard lock { queue_mutex };
	if ( queued.empty() && inflight.empty() ) drained.post();
}

bool DBConnection::hasWork() const
{
	std::lock_guard lock { queue_mutex };
	return !queued.empty() || !inflight.empty();
}

DBConnection::DBConnection( const std::uint64_t id, const ConnectionRole role, DBConfig config, DBReactor& reactor ) :
  id( id ),
  role( role ),
  config( std::move( config ) ),
  reactor( reactor )
{}

DBConnection::~DBConnection()
{
	if ( connection ) PQfinish( connection );
}

runtime::Task<> DBConnection::connectImpl( std::shared_ptr< DBConnection > self )
{
	ConnectionState expected { ConnectionState::cold };
	if ( self->state.compare_exchange_strong( expected, ConnectionState::connecting, std::memory_order_acq_rel ) )
		self->wake();

	co_await self->connected;
	if ( self->connect_error ) std::rethrow_exception( self->connect_error );
}

runtime::Task<> DBConnection::connect()
{
	return connectImpl( shared_from_this() );
}

void DBConnection::enqueue( std::shared_ptr< DBOperation > operation )
{
	if ( !operation ) throw std::invalid_argument( "Cannot enqueue a null database operation" );
	operation->markQueued();

	{
		std::lock_guard lock( queue_mutex );
		drained.reset();
		queued.push_back( std::move( operation ) );
	}
	wake();
}

void DBConnection::wake()
{
	reactor.schedule( shared_from_this() );
}

void DBConnection::pump()
{
	const auto state { this->state.load( std::memory_order_acquire ) };

	switch ( state )
	{
		case ConnectionState::connecting:
			[[fallthrough]];
		case ConnectionState::configuring:
			pumpHandshake( state );
			return;
		case ConnectionState::connected:
			pumpConnected();
			return;
		default:
			return;
	}
}

void DBConnection::pumpConnected()
{
	constexpr std::size_t dispatch_budget { pump_operation_budget };
	bool use_pipeline { role != ConnectionRole::management };
	for ( std::size_t dispatched = 0; dispatched < dispatch_budget; ++dispatched )
	{
		std::shared_ptr< DBOperation > operation { popDispatchable( use_pipeline ) };
		if ( !operation ) break;

		if ( operation->phase() == OperationPhase::completed || operation->phase() == OperationPhase::abandoned )
		{
			postDrainedIfIdle();
			continue;
		}

		if ( !operation->send( connection, use_pipeline ) )
			throw connectionError( connection, "Could not send PostgreSQL operation" );

		pushInflight( std::move( operation ) );
	}

	const int flush { PQflush( connection ) };
	if ( flush < 0 ) throw connectionError( connection, "Could not flush PostgreSQL operations" );

	if ( PQconsumeInput( connection ) != 1 )
		throw connectionError( connection, "Could not read PostgreSQL operation results" );

	if ( PQisBusy( connection ) == 0 )
	{
		bool advanced_delimiter { false };
		for ( std::size_t retrieved = 0; retrieved < pump_operation_budget; ++retrieved )
		{
			PGresult* raw { PQgetResult( connection ) };
			if ( raw == nullptr )
			{
				std::shared_ptr< DBOperation > head { getHead() };

				if ( use_pipeline && head && head->needsSynchronization() && !advanced_delimiter )
				{
					advanced_delimiter = true;
					continue;
				}

				if ( !use_pipeline && head )
				{
					head->complete();
					retireHead();
				}
				break;
			}

			advanced_delimiter = false;
			const auto status { PQresultStatus( raw ) };
			std::shared_ptr< DBOperation > head { getHead() };
			if ( !head )
			{
				PQclear( raw );
				throw std::runtime_error( "PostgreSQL returned a result without an operation ledger entry" );
			}

			if ( status == PGRES_PIPELINE_SYNC )
			{
				PQclear( raw );
				head->complete();
				retireHead();
				continue;
			}

			if ( status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK )
			{
				head->acceptResult( DBResult { raw } );
				continue;
			}

			const auto error { queryError( raw, head->id() ) };
			PQclear( raw );
			head->acceptFailure( error );
		}
	}

	if ( hasWork() )
	{
		const auto socket { PQsocket( connection ) };
		if ( socket < 0 ) throw connectionError( connection, "PostgreSQL connection has no socket" );

		reactor.watch( shared_from_this(), socket, flush == 0 ? SocketInterest::read : SocketInterest::read_write );
	}
}

void DBConnection::pumpHandshake( const ConnectionState state )
{
	try
	{
		if ( state == ConnectionState::connecting && connection == nullptr )
		{
			const auto port { std::to_string( config.port ) };
			const std::array< const char*, 7 > keywords {
				"host", "port", "dbname", "user", "password", "client_encoding", nullptr
			};
			const std::array< const char*, 7 > values {
				config.host.c_str(),
				port.c_str(),
				config.database.c_str(),
				config.user.c_str(),
				config.password.c_str(),
				"UTF8",
				nullptr
			};
			connection = PQconnectStartParams( keywords.data(), values.data(), 0 );
			if ( connection == nullptr ) throw connectionError( nullptr, "Could not start PostgreSQL connection" );
		}

		if ( state == ConnectionState::connecting )
		{
			const auto status { PQconnectPoll( connection ) };
			if ( status == PGRES_POLLING_READING || status == PGRES_POLLING_WRITING || status == PGRES_POLLING_ACTIVE )
			{
				const auto socket { PQsocket( connection ) };
				if ( socket < 0 ) throw connectionError( connection, "PostgreSQL connection has no socket" );

				const auto interest {
					status == PGRES_POLLING_READING ? SocketInterest::read :
					status == PGRES_POLLING_WRITING ? SocketInterest::write :
													  SocketInterest::read_write
				};
				reactor.watch( shared_from_this(), socket, interest );
				return;
			}

			if ( status == PGRES_POLLING_FAILED ) throw connectionError( connection, "PostgreSQL connection failed" );

			if ( status != PGRES_POLLING_OK ) throw std::runtime_error( "Unexpected PostgreSQL connection poll state" );

			if ( PQsetnonblocking( connection, 1 ) != 0 )
				throw connectionError( connection, "Could not enable PostgreSQL nonblocking mode" );

			if ( role != ConnectionRole::management && PQenterPipelineMode( connection ) != 1 )
				throw connectionError( connection, "Could not enter PostgreSQL pipeline mode" );

			const auto timeout =
				std::to_string( role == ConnectionRole::management ? 0 : config.statement_timeout.count() );
			const std::array< const char*, 2 > values { timeout.c_str(), config.schema.c_str() };
			constexpr std::array< int, 2 > lengths { 0, 0 };
			constexpr std::array< int, 2 > formats { 0, 0 };
			// Values are parsed by PostgreSQL as data; no connection information or
			// SQL configuration text is constructed by concatenation.
			constexpr char startup_sql[] =
				"SELECT set_config('statement_timeout', $1, false), set_config('search_path', $2, false)";
			if ( PQsendQueryParams(
					 connection, startup_sql, 2, nullptr, values.data(), lengths.data(), formats.data(), 0 )
			     != 1 )
				throw connectionError( connection, "Could not configure PostgreSQL connection" );

			if ( role != ConnectionRole::management && PQpipelineSync( connection ) != 1 )
				throw connectionError( connection, "Could not synchronize PostgreSQL startup configuration" );

			this->state.store( ConnectionState::configuring, std::memory_order_release );
		}

		const int flush { PQflush( connection ) };
		if ( flush < 0 ) throw connectionError( connection, "Could not flush PostgreSQL startup configuration" );

		if ( PQconsumeInput( connection ) != 1 )
			throw connectionError( connection, "Could not read PostgreSQL startup configuration" );

		if ( PQisBusy( connection ) == 0 )
		{
			bool startup_delimiter { false };
			bool reading_results { true };
			while ( reading_results )
			{
				PGresult* result { PQgetResult( connection ) };
				if ( result == nullptr )
				{
					// Pipeline mode emits a null delimiter after a command's result;
					// our ledger knows the startup query is immediately followed by
					// its Sync, so advance once to observe that boundary. A null is
					// not itself completion while a known Sync remains.
					if ( role != ConnectionRole::management && !saw_startup_sync && !startup_delimiter )
					{
						startup_delimiter = true;
						continue;
					}
					reading_results = false;
					continue;
				}

				const auto result_status { PQresultStatus( result ) };
				if ( result_status == PGRES_PIPELINE_SYNC )
					saw_startup_sync = true;
				else if ( result_status != PGRES_TUPLES_OK && result_status != PGRES_COMMAND_OK )
				{
					const std::string detail { PQresultErrorMessage( result ) };
					PQclear( result );
					throw std::runtime_error( "PostgreSQL startup configuration failed: " + detail );
				}
				PQclear( result );
			}
		}

		const bool configured = role == ConnectionRole::management ? PQisBusy( connection ) == 0 : saw_startup_sync;
		if ( configured )
		{
			this->state.store( ConnectionState::connected, std::memory_order_release );
			connected.post();
			return;
		}

		const auto socket { PQsocket( connection ) };
		if ( socket < 0 ) throw connectionError( connection, "PostgreSQL connection has no socket" );
		reactor.watch( shared_from_this(), socket, flush == 0 ? SocketInterest::read : SocketInterest::read_write );
	}
	catch ( ... )
	{
		connect_error = std::current_exception();
		this->state.store( ConnectionState::failed, std::memory_order_release );
		connected.post();
	}
}

std::shared_ptr< DBOperation > DBConnection::popDispatchable( bool use_pipeline )
{
	std::lock_guard lock { queue_mutex };
	if ( queued.empty() || ( !use_pipeline && !inflight.empty() ) ) return nullptr;

	auto operation { std::move( queued.front() ) };
	queued.pop_front();
	return operation;
}

ConnectionStats DBConnection::stats() const
{
	std::lock_guard lock( queue_mutex );
	return {
		.id = id,
		.queued = queued.size(),
		.sent = sent,
		.unfinished = queued.size() + sent,
		.input_paused = input_paused,
	};
}

runtime::Task<> DBConnection::drainImpl( std::shared_ptr< DBConnection > self )
{
	bool fully_drained { false };
	while ( !fully_drained )
	{
		{
			std::lock_guard lock( self->queue_mutex );
			fully_drained = self->queued.empty() && self->inflight.empty();
		}
		if ( !fully_drained ) co_await self->drained;
	}
}

runtime::Task<> DBConnection::drain()
{
	return drainImpl( shared_from_this() );
}

runtime::Task<> DBConnection::restoreLease()
{
	return drainImpl( shared_from_this() );
}
} // namespace idhan::db
