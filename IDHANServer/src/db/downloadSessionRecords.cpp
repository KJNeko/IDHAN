#include "downloadSessionRecords.hpp"

#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBTransaction.hpp"

namespace idhan::db
{
runtime::Task< RecordedSessionImport > recordDownloadSessionImport(
	DBContext& db,
	const DownloadSessionID session_id,
	const RecordID record_id,
	const std::optional< std::int64_t > posted_at,
	const bool imported )
{
	auto transaction { co_await db.transaction() };
	DBBindings session;
	session.bind( session_id );
	const auto counters {
		co_await transaction
			.query(
				"SELECT total_imported_count FROM download_sessions WHERE download_session_id = $1 FOR UPDATE",
				std::move( session ) )
			.rows< std::int64_t >()
	};
	const auto& [ total ] { counters.row( 0 ) };

	DBBindings previous;
	previous.bind( session_id );
	previous.bind( record_id );
	const auto records {
		co_await transaction
			.query(
				"SELECT was_imported FROM download_session_records WHERE download_session_id = $1 AND record_id = $2",
				std::move( previous ) )
			.rows< bool >()
	};
	const bool newly_imported { imported && ( records.empty() || !std::get< 0 >( records.row( 0 ) ) ) };

	DBBindings bindings;
	bindings.bind( session_id );
	bindings.bind( record_id );
	bindings.bind( posted_at );
	bindings.bind( imported );
	co_await transaction
		.query(
			//language=postgresql
			R"SQL(
					INSERT INTO download_session_records (download_session_id, record_id, posted_at, post_dates_complete, was_imported)
					    VALUES ($1, $2, to_timestamp($3::double precision), $3 IS NOT NULL, $4)
					ON CONFLICT (download_session_id, record_id)
					    DO UPDATE SET
					        was_imported = download_session_records.was_imported OR EXCLUDED.was_imported,
					        posted_at = GREATEST (download_session_records.posted_at, EXCLUDED.posted_at),
					        post_dates_complete = download_session_records.post_dates_complete
					        AND EXCLUDED.post_dates_complete
				)SQL",
			std::move( bindings ) )
		.result();
	if ( newly_imported )
	{
		DBBindings increment;
		increment.bind( session_id );
		co_await transaction
			.query(
				"UPDATE download_sessions SET total_imported_count = total_imported_count + 1 WHERE download_session_id = $1",
				std::move( increment ) )
			.result();
	}

	co_await transaction.commit();
	co_return RecordedSessionImport { records.empty(), total + newly_imported };
}
} // namespace idhan::db
