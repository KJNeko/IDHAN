#include "idhan/db/DBContext.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>

#include "idhan/db/DBOperation.hpp"
#include "idhan/db/DBReactor.hpp"

namespace idhan::db
{

DBContext::DBContext( DBConfig config, folly::Executor::KeepAlive<> application_executor ) :
  m_config( std::move( config ) ),
  m_application_executor( std::move( application_executor ) ),
  m_reactor(
	  std::make_shared< DBReactor >(
		  m_config.worker_threads == 0 ?
			  std::min(
				  m_config.connections + m_config.transaction_connections,
				  std::max< std::size_t >( 2, std::thread::hardware_concurrency() / 2 ) ) :
			  m_config.worker_threads ) ),
  m_pool( std::make_shared< DBPool >( m_config, m_reactor, m_application_executor ) )
{
	if ( s_instance != nullptr ) throw std::runtime_error( "A DBContext already exists for this process" );

	s_instance = this;
}

DBContext::~DBContext()
{
	if ( s_instance == this ) s_instance = nullptr;
}

DBContext& DBContext::instance()
{
	if ( s_instance == nullptr ) throw std::runtime_error( "No DBContext has been constructed yet" );

	return *s_instance;
}

runtime::Task<> DBContext::start()
{
	if ( PQisthreadsafe() != 1 ) throw std::runtime_error( "libpq must be built with thread safety enabled" );

	m_reactor->start();
	try
	{
		co_await m_pool->start();
	}
	catch ( ... )
	{
		m_reactor->stop();
		throw;
	}
}

DBTask DBContext::query( std::string sql, DBBindings bindings, QueryOptions options )
{
	auto operation {
		std::make_shared< DBOperation >( std::move( sql ), std::move( bindings ), options, m_application_executor )
	};

	auto pool { m_pool };
	operation->setStarter( [ pool = std::move( pool ) ]( std::shared_ptr< DBOperation > queued )
	                       { pool->submit( std::move( queued ) ); } );

	return DBTask { std::move( operation ) };
}

runtime::Task< DBTransaction > DBContext::transaction()
{
	co_return co_await m_pool->transaction();
}

std::vector< ConnectionStats > DBContext::stats() const
{
	return m_pool->stats();
}

runtime::Task<> DBContext::drain()
{
	try
	{
		co_await m_pool->drain();
	}
	catch ( ... )
	{
		m_reactor->stop();
		throw;
	}

	m_reactor->stop();
}

} // namespace idhan::db
