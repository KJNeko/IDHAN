#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
class DBTransaction;
} // namespace idhan::db

namespace idhan::downloader
{
struct GeneratorRequest
{
	std::string generator;
	std::string origin;
	std::map< std::string, std::string > parameters;
};

//! Persist a subscription's reusable downloader session under the scheduler's transaction.
[[nodiscard]] runtime::Task< DownloadSessionID > createGeneratorSession(
	db::DBTransaction& transaction,
	SubscriptionID subscription_id,
	const GeneratorRequest& request );

runtime::Task<> configureGeneratorSession(
	db::DBTransaction& transaction,
	DownloadSessionID session_id,
	const GeneratorRequest& request );
[[nodiscard]] runtime::Task< std::vector< DownloadSessionID > > subscriptionSessionIds( db::DBContext& db );
[[nodiscard]] runtime::Task< std::vector< RecordID > > generatorRecords(
	db::DBContext& db,
	DownloadSessionID session_id );
} // namespace idhan::downloader
