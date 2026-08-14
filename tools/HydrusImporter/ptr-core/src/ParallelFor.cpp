#include "ptr/flatten/ParallelFor.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace idhan::hydrus::ptr
{

unsigned defaultWorkerCount()
{
	return std::max( 1u, std::thread::hardware_concurrency() );
}

bool parallelIndexed( const std::size_t count,
                      const unsigned threads,
                      const std::function< bool( std::size_t ) >& body )
{
	if ( count == 0 ) return true;

	// Never spawn more workers than there is work; harmless but pointless for a small corpus.
	const auto requested = threads != 0 ? threads : defaultWorkerCount();
	const auto workers = static_cast< unsigned >( std::min< std::size_t >( requested, count ) );

	std::atomic< std::size_t > next { 0 };
	std::atomic< bool > stop { false };

	std::mutex error_mutex;
	std::exception_ptr error;

	const auto lane = [ & ]
	{
		while ( !stop.load( std::memory_order_relaxed ) )
		{
			const auto index = next.fetch_add( 1, std::memory_order_relaxed );
			if ( index >= count ) return;

			try
			{
				if ( !body( index ) ) stop.store( true, std::memory_order_relaxed );
			}
			catch ( ... )
			{
				{
					std::lock_guard< std::mutex > lock { error_mutex };
					if ( !error ) error = std::current_exception();
				}

				stop.store( true, std::memory_order_relaxed );
				return;
			}
		}
	};

	{
		std::vector< std::jthread > pool;
		pool.reserve( workers );
		for ( unsigned worker = 0; worker < workers; ++worker ) pool.emplace_back( lane );
		// pool's destructor joins every thread here, so nothing below runs while a lane is live.
	}

	if ( error ) std::rethrow_exception( error );

	return !stop.load( std::memory_order_relaxed );
}

} // namespace idhan::hydrus::ptr
