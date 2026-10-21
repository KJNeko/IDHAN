#include "idhan/db/DBPool.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "IDHANDB.hpp"
#include "idhan/db/DBOperation.hpp"
#include "idhan/db/DBReactor.hpp"
#include "idhan/db/DBTransaction.hpp"

namespace idhan::db
{
DBPool::DBPool(
	DBConfig config,
	std::shared_ptr< DBReactor > reactor,
	folly::Executor::KeepAlive<> application_executor ) :
  m_config( std::move( config ) ),
  m_reactor( std::move( reactor ) ),
  m_application_executor( std::move( application_executor ) )
{}

DBPool::~DBPool() = default;

runtime::Task<> DBPool::start()
{
	if ( !m_reactor ) throw std::invalid_argument( "Database pool requires a reactor" );
	m_config.validate();
	std::vector< std::shared_ptr< DBConnection > > normal;
	std::vector< std::shared_ptr< DBConnection > > transactions;
	{
		std::lock_guard lock( m_mutex );
		if ( m_started ) co_return;
		if ( m_starting ) throw std::logic_error( "Database pool startup is already in progress" );
		if ( m_draining ) throw std::logic_error( "Cannot start a draining database pool" );
		m_starting = true;

		std::uint64_t id { 1 };
		normal.reserve( m_config.connections );
		for ( std::size_t index = 0; index < m_config.connections; ++index )
			normal.push_back( std::make_shared< DBConnection >( id++, ConnectionRole::normal, m_config, *m_reactor ) );

		transactions.reserve( m_config.transaction_connections );
		for ( std::size_t index = 0; index < m_config.transaction_connections; ++index )
			transactions.push_back(
				std::make_shared< DBConnection >( id++, ConnectionRole::transaction, m_config, *m_reactor ) );
	}

	// Startup is before HTTP admission. Sequential awaits simplify failure
	// propagation; all query-time connection progress remains concurrent.
	try
	{
		for ( const auto& connection : normal ) co_await connection->connect();
		for ( const auto& connection : transactions ) co_await connection->connect();
	}
	catch ( ... )
	{
		std::lock_guard lock( m_mutex );
		m_starting = false;
		throw;
	}

	{
		std::lock_guard lock( m_mutex );
		m_normal = std::move( normal );
		m_transactions = std::move( transactions );
		m_available_transactions = std::make_shared< folly::coro::BoundedQueue< std::shared_ptr< DBConnection > > >(
			static_cast< std::uint32_t >( m_transactions.size() ) );
		for ( const auto& connection : m_transactions )
			if ( !m_available_transactions->try_enqueue( connection ) )
				throw std::logic_error( "Could not initialize database transaction leases" );
		m_starting = false;
		m_started = true;
	}
}

runtime::Task< DBTransaction > DBPool::transaction()
{
	std::shared_ptr< folly::coro::BoundedQueue< std::shared_ptr< DBConnection > > > leases;
	{
		std::lock_guard lock( m_mutex );
		if ( !m_started || m_draining || !m_available_transactions )
			throw std::logic_error( "Database pool is not accepting transactions" );
		leases = m_available_transactions;
	}

	auto connection { co_await leases->dequeue() };
	auto operation {
		std::make_shared< DBOperation >( "BEGIN", DBBindings {}, QueryOptions {}, m_application_executor )
	};
	operation->setStarter( [ connection ]( std::shared_ptr< DBOperation > queued )
	                       { connection->enqueue( std::move( queued ) ); } );

	try
	{
		co_await DBTask { std::move( operation ) }.result();
	}
	catch ( ... )
	{
		if ( !leases->try_enqueue( std::move( connection ) ) )
			throw std::logic_error( "Could not return a failed database transaction lease" );
		throw;
	}

	auto release = [ leases ]( std::shared_ptr< DBConnection > returned )
	{
		if ( !leases->try_enqueue( std::move( returned ) ) )
			throw std::logic_error( "Database transaction lease was returned more than once" );
	};
	co_return DBTransaction::create( std::move( connection ), m_application_executor, std::move( release ) );
}

void DBPool::submit( std::shared_ptr< DBOperation > operation )
{
	if ( !operation ) throw std::invalid_argument( "Cannot submit a null database operation" );

	std::shared_ptr< DBConnection > selected;
	{
		std::lock_guard lock( m_mutex );
		if ( !m_started || m_normal.empty() || m_draining )
			throw std::logic_error( "Database pool is not accepting operations" );

		std::size_t minimum { std::numeric_limits< std::size_t >::max() };
		for ( const auto& connection : m_normal ) minimum = std::min( minimum, connection->stats().unfinished );

		for ( std::size_t offset = 0; offset < m_normal.size(); ++offset )
		{
			const auto index { ( m_next_tie + offset ) % m_normal.size() };
			if ( m_normal[ index ]->stats().unfinished != minimum ) continue;

			selected = m_normal[ index ];
			m_next_tie = ( index + 1 ) % m_normal.size();
			break;
		}
	}
	selected->enqueue( std::move( operation ) );
}

runtime::Task<> DBPool::drain()
{
	std::vector< std::shared_ptr< DBConnection > > connections;
	{
		std::lock_guard lock( m_mutex );
		m_draining = true;
		connections.reserve( m_normal.size() + m_transactions.size() );
		connections.insert( connections.end(), m_normal.begin(), m_normal.end() );
		connections.insert( connections.end(), m_transactions.begin(), m_transactions.end() );
	}
	for ( const auto& connection : connections ) co_await connection->drain();
}

std::vector< ConnectionStats > DBPool::stats() const
{
	std::lock_guard lock( m_mutex );
	std::vector< ConnectionStats > result;
	result.reserve( m_normal.size() + m_transactions.size() );
	for ( const auto& connection : m_normal ) result.push_back( connection->stats() );
	for ( const auto& connection : m_transactions ) result.push_back( connection->stats() );
	return result;
}
} // namespace idhan::db
