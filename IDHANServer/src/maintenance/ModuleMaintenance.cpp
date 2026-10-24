#include "ModuleMaintenance.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "logging/log.hpp"
#include "modules/ModuleLoader.hpp"

namespace idhan::maintenance
{
ModuleMaintenance::ModuleMaintenance( modules::ModuleLoader& loader ) :
  m_loader( loader ),
  m_thread( [ this ]( const std::stop_token stop ) { run( stop ); } )
{}

ModuleMaintenance::~ModuleMaintenance()
{
	m_job.activity( "stopping", "Waiting for module worker maintenance to finish" );
	m_thread.request_stop();
	if ( m_thread.joinable() ) m_thread.join();
}

void ModuleMaintenance::run( const std::stop_token stop )
{
	std::mutex mutex;
	std::condition_variable_any wake;
	std::unique_lock lock { mutex };
	m_job.activity( "waiting", "Module worker maintenance runs every 30 seconds" );
	while ( !stop.stop_requested() )
	{
		wake.wait_for( lock, stop, std::chrono::seconds { 30 }, [] { return false; } );
		if ( stop.stop_requested() ) break;

		try
		{
			m_job.activity( "working", "Checking module workers and retiring idle processes" );
			m_loader.maintainWorkers();
			m_job.activity( "waiting", "Waiting for the next module worker maintenance pass" );
		}
		catch ( const std::exception& error )
		{
			m_job.recordFailure( error.what() );
			m_job.activity( "retrying", "Worker maintenance failed; retrying in 30 seconds" );
			log::warn( "Module worker maintenance failed: {}", error.what() );
		}
	}
	m_job.finish();
}
} // namespace idhan::maintenance
