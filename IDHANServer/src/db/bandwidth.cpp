#include "bandwidth.hpp"

#include "idhan/db/DBContext.hpp"

namespace idhan::db
{
runtime::Task<> storeBandwidth( DBContext& db, const std::vector< downloader::BandwidthLog >& logs )
{
	std::vector< std::string > hosts;
	std::vector< std::int64_t > timestamps, received;
	for ( const auto& log : logs )
	{
		hosts.push_back( log.host );
		timestamps.push_back( log.timestamp );
		received.push_back( log.bytes_received );
	}
	auto transaction { co_await db.transaction() };
	DBBindings host_bindings;
	host_bindings.bind( hosts );
	// Separate statements give the log insert a fresh snapshot after a concurrent
	// host insertion finishes. Sort hosts so overlapping batches lock in the same order.
	co_await transaction
		.query(
			"INSERT INTO bandwidth_hosts (host) "
			"SELECT DISTINCT input.host FROM unnest($1::text[]) AS input(host) "
			"WHERE NOT EXISTS (SELECT 1 FROM bandwidth_hosts AS hosts WHERE hosts.host = input.host) "
			"ORDER BY input.host "
			"ON CONFLICT (host) DO NOTHING",
			std::move( host_bindings ) )
		.result();

	DBBindings bindings;
	bindings.bind( hosts );
	bindings.bind( timestamps );
	bindings.bind( received );
	co_await transaction
		.query(
			"INSERT INTO bandwidth_logs (host_id, timestamp, bytes_received) "
			"SELECT hosts.host_id, input.timestamp, sum(input.bytes_received)::bigint "
			"FROM unnest($1::text[], $2::bigint[], $3::bigint[]) AS input(host, timestamp, bytes_received) "
			"JOIN bandwidth_hosts AS hosts ON hosts.host = input.host "
			"GROUP BY hosts.host_id, input.timestamp ORDER BY hosts.host_id, input.timestamp "
			"ON CONFLICT (host_id, timestamp) DO UPDATE "
			"SET bytes_received = bandwidth_logs.bytes_received + EXCLUDED.bytes_received",
			std::move( bindings ) )
		.result();
	co_await transaction.commit();
}

runtime::Task< std::vector< HostBandwidth > > bandwidthTotals(
	DBContext& db,
	const std::int64_t start,
	const std::int64_t end )
{
	DBBindings bindings;
	bindings.bind( start );
	bindings.bind( end );
	const auto rows {
		co_await db
			.query(
				"SELECT hosts.host, sum(logs.bytes_received)::bigint "
				"FROM bandwidth_logs AS logs JOIN bandwidth_hosts AS hosts USING (host_id) "
				"WHERE logs.timestamp >= $1 AND logs.timestamp < $2 GROUP BY hosts.host ORDER BY hosts.host",
				std::move( bindings ) )
			.rows< std::string, std::int64_t >()
	};
	std::vector< HostBandwidth > totals;
	for ( const auto& [ host, received ] : rows ) totals.push_back( { host, received } );

	co_return totals;
}
} // namespace idhan::db
