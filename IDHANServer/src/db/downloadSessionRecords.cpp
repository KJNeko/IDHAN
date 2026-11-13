#include "downloadSessionRecords.hpp"

#include "idhan/db/DBContext.hpp"

namespace idhan::db
{
runtime::Task< bool > recordDownloadSessionImport(
	DBContext& db,
	const DownloadSessionID session_id,
	const RecordID record_id,
	const std::optional< std::int64_t > posted_at )
{
	DBBindings bindings;
	bindings.bind( session_id );
	bindings.bind( record_id );
	bindings.bind( posted_at );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					INSERT INTO download_session_records (download_session_id, record_id, posted_at, post_dates_complete)
					    VALUES ($1, $2, to_timestamp($3::double precision), $3 IS NOT NULL)
					ON CONFLICT (download_session_id, record_id)
					    DO UPDATE SET
					        posted_at = GREATEST (download_session_records.posted_at, EXCLUDED.posted_at),
					        post_dates_complete = download_session_records.post_dates_complete
					        AND EXCLUDED.post_dates_complete
					    RETURNING (xmax = 0)
				)SQL",
				std::move( bindings ) )
			.rows< bool >()
	};
	co_return std::get< 0 >( rows.row( 0 ) );
}
} // namespace idhan::db
