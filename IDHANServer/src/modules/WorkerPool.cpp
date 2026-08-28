#include "WorkerPool.hpp"

#include <format>

#include "logging/log.hpp"

namespace idhan::modules
{

WorkerPool::WorkerPool(
	WorkerSettings settings,
	const ModuleResidency residency,
	const std::size_t rss_limit_kb,
	const std::chrono::seconds idle_timeout,
	WorkerProcess::CallbackHandler on_callback ) :
  m_settings( std::move( settings ) ),
  m_residency( residency ),
  m_on_callback( std::move( on_callback ) ),
  m_rss_limit_kb( rss_limit_kb ),
  m_idle_timeout( idle_timeout )
{}

std::expected< std::shared_ptr< WorkerProcess >, std::string > WorkerPool::acquire( const bool isolated )
{
	const std::lock_guard< std::mutex > guard { m_mutex };

	if ( m_shutting_down ) return std::unexpected( std::string { "module system is shutting down" } );

	if ( !isolated && m_residency == ModuleResidency::PERSISTENT && m_worker != nullptr && m_worker->alive() )
		return m_worker;

	auto worker { std::make_shared< WorkerProcess >( m_settings, m_on_callback ) };

	if ( const auto started { worker->start() }; !started ) return std::unexpected( started.error() );

	if ( !isolated && m_residency == ModuleResidency::PERSISTENT ) m_worker = worker;

	return worker;
}

void WorkerPool::prewarm()
{
	if ( m_residency != ModuleResidency::PERSISTENT ) return;

	if ( const auto worker { acquire() }; !worker )
		log::warn( "Could not pre-warm the worker for {}: {}", m_settings.library.string(), worker.error() );
}

IDHANTask< std::shared_ptr< CallOutcome > > WorkerPool::dispatch(
	Json::Value body,
	std::shared_ptr< const CallInput > input,
	const bool isolated )
{
	for ( int attempt = 0; attempt < 2; ++attempt )
	{
		auto worker { acquire( isolated ) };

		if ( !worker )
		{
			auto outcome { std::make_shared< CallOutcome >() };
			outcome->ok = false;
			outcome->error = worker.error();
			outcome->worker_died = true;
			co_return outcome;
		}

		auto manifest { co_await ( *worker )->awaitManifestAsync( m_settings.liveness_grace ) };
		if ( !manifest )
		{
			( *worker )->terminate( "worker did not provide a valid startup manifest" );

			if ( attempt == 0 ) continue;

			auto outcome { std::make_shared< CallOutcome >() };
			outcome->ok = false;
			outcome->error = manifest.error();
			outcome->worker_died = true;
			co_return outcome;
		}

		auto outcome { co_await ( *worker )->call( body, input ) };

		const bool single_use { isolated || m_residency == ModuleResidency::SINGLE_RUN };
		if ( single_use ) ( *worker )->terminate( "single-run worker finished its call", Termination::EXPECTED );

		if ( outcome->ok || !outcome->worker_died ) co_return outcome;

		if ( attempt == 0 )
		{
			log::warn(
				"Module worker for {} died during a call ({}); retrying once in a fresh process",
				m_settings.library.string(),
				outcome->error );

			{
				const std::lock_guard< std::mutex > guard { m_mutex };
				if ( m_worker == *worker ) m_worker.reset();
			}

			continue;
		}

		co_return outcome;
	}

	auto outcome { std::make_shared< CallOutcome >() };
	outcome->ok = false;
	outcome->error = std::format( "module worker for {} could not be kept alive", m_settings.library.string() );
	outcome->worker_died = true;
	co_return outcome;
}

void WorkerPool::maintain()
{
	std::shared_ptr< WorkerProcess > retiring {};
	std::string reason {};

	{
		const std::lock_guard< std::mutex > guard { m_mutex };

		if ( m_worker == nullptr || !m_worker->alive() ) return;

		if ( m_worker->activeCalls() > 0 ) return;

		if ( m_worker->rssKb() > m_rss_limit_kb )
			reason = std::format( "resident set {} KiB is over the {} KiB ceiling", m_worker->rssKb(), m_rss_limit_kb );
		else if ( std::chrono::steady_clock::now() - m_worker->lastActivity() > m_idle_timeout )
			reason = std::format( "idle for more than {}s", m_idle_timeout.count() );

		if ( reason.empty() ) return;

		retiring = std::move( m_worker );
		m_worker.reset();
	}

	log::info( "Retiring the module worker for {}: {}", m_settings.library.string(), reason );
	retiring->terminate( reason, Termination::EXPECTED );
}

void WorkerPool::shutdown()
{
	std::shared_ptr< WorkerProcess > worker {};

	{
		const std::lock_guard< std::mutex > guard { m_mutex };
		m_shutting_down = true;
		worker = std::move( m_worker );
		m_worker.reset();
	}

	if ( worker != nullptr ) worker->terminate( "server is shutting down", Termination::EXPECTED );
}

} // namespace idhan::modules
