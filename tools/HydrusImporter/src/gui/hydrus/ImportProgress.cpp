#include "ImportProgress.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace idhan::hydrus
{

constexpr auto RECENT_PROGRESS_WINDOW = std::chrono::minutes( 1 );

void ImportProgress::start( const std::size_t total, const TimePoint now )
{
	m_start = now;
	m_last_batch = now;
	m_total = total;
	m_processed = 0;
	m_batches.clear();
}

void ImportProgress::advance( const std::size_t count, const TimePoint now )
{
	if ( !m_start ) return;

	const auto completed = std::min( count, m_total - m_processed );
	if ( completed == 0 ) return;

	const auto end = std::max( now, m_last_batch );
	m_batches.emplace_back( m_last_batch, end, completed );
	m_processed += completed;
	m_last_batch = end;
	const auto cutoff = end - RECENT_PROGRESS_WINDOW;
	while ( !m_batches.empty() && m_batches.front().end < cutoff ) m_batches.pop_front();
}

ImportProgress::Snapshot ImportProgress::snapshot( const TimePoint now ) const
{
	Snapshot result { .processed = m_processed, .total = m_total };
	if ( !m_start ) return result;

	if ( m_processed == m_total ) result.remaining = std::chrono::seconds( 0 );
	const auto elapsed = std::chrono::duration< double >( now - *m_start ).count();
	if ( elapsed <= 0 ) return result;

	result.average_per_second = static_cast< double >( m_processed ) / elapsed;
	const auto cutoff = std::max( *m_start, now - RECENT_PROGRESS_WINDOW );
	const auto window_seconds = std::chrono::duration< double >( now - cutoff ).count();
	double recent_count { 0 };
	for ( const auto& batch : m_batches )
	{
		if ( batch.begin == batch.end )
		{
			if ( batch.end <= now && now - batch.end < RECENT_PROGRESS_WINDOW )
				recent_count += static_cast< double >( batch.count );
			continue;
		}

		if ( batch.end <= cutoff ) continue;

		// Attribute a completed batch across its full processing interval, including reads and server waits.
		// Prorating a batch that spans the window avoids reporting a spike when a long request completes.
		const auto overlap =
			std::chrono::duration< double >( std::min( now, batch.end ) - std::max( cutoff, batch.begin ) ).count();
		if ( overlap <= 0 ) continue;

		const auto batch_seconds = std::chrono::duration< double >( batch.end - batch.begin ).count();
		recent_count += static_cast< double >( batch.count ) * overlap / batch_seconds;
	}

	result.recent_per_second = recent_count / window_seconds;
	if ( m_processed == m_total ) return result;

	if ( result.recent_per_second <= 0 ) return result;

	const auto seconds = std::ceil( static_cast< double >( m_total - m_processed ) / result.recent_per_second );
	if ( !std::isfinite( seconds ) ) return result;

	if ( seconds >= static_cast< double >( std::numeric_limits< std::chrono::seconds::rep >::max() ) ) return result;

	result.remaining = std::chrono::seconds( static_cast< std::chrono::seconds::rep >( seconds ) );
	return result;
}

} // namespace idhan::hydrus
