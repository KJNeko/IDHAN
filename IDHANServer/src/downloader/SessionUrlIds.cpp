#include "SessionUrlIds.hpp"

#include "idhan/db/DBContext.hpp"

namespace idhan::downloader
{
SessionUrlIds& SessionUrlIds::instance()
{
	static SessionUrlIds ids {};
	return ids;
}

std::atomic< DownloadSessionUrlID >& SessionUrlIds::counter( const DownloadSessionID session_id )
{
	const std::scoped_lock lock { m_mutex };
	// A session missing at load time had no URLs, so it starts from the first id.
	return m_next.try_emplace( session_id, 1 ).first->second;
}

runtime::Task< void > SessionUrlIds::load( db::DBContext& db )
{
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    download_session_id,
					    max(download_session_url_id)
					FROM download_session_urls
					GROUP BY
					    download_session_id
				)SQL" )
			.rows< DownloadSessionID, DownloadSessionUrlID >()
	};

	const std::scoped_lock lock { m_mutex };
	for ( const auto& [ session_id, highest ] : rows ) m_next.try_emplace( session_id, highest + 1 );
}

void SessionUrlIds::create( const DownloadSessionID session_id )
{
	const std::scoped_lock lock { m_mutex };
	m_next.try_emplace( session_id, 1 );
}

void SessionUrlIds::ensureNext( const DownloadSessionID session_id, const DownloadSessionUrlID minimum )
{
	auto& next { counter( session_id ) };
	auto current { next.load() };
	while ( current < minimum && !next.compare_exchange_weak( current, minimum ) )
	{}
}

void SessionUrlIds::remove( const DownloadSessionID session_id )
{
	const std::scoped_lock lock { m_mutex };
	m_next.erase( session_id );
}

DownloadSessionUrlID SessionUrlIds::reserve( const DownloadSessionID session_id, const std::size_t count )
{
	return counter( session_id ).fetch_add( static_cast< DownloadSessionUrlID >( count ) );
}
} // namespace idhan::downloader
