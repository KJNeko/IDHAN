#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"
#include "jobs/JobTask.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::embeddings
{

struct EmbeddingModelInfo
{
	std::int32_t id;
	std::int32_t dimensions;
};

[[nodiscard]] runtime::Task< std::optional< EmbeddingModelInfo > > findEmbeddingModel(
	std::string_view model_name,
	db::DBContext& db );

runtime::Task< void > registerEmbeddingModels( db::DBContext& db );

//! Claims model_id for a backfill. False if one is already running for it.
[[nodiscard]] bool tryBeginBackfill( std::int32_t model_id );

//! Releases a claim taken by tryBeginBackfill.
void endBackfill( std::int32_t model_id );

//! Sweeps every eligible record missing an embedding under model_id and embeds it.
//! Caller must hold a tryBeginBackfill claim for model_id; the job releases it on completion.
[[nodiscard]] JobTask backfillJob( std::int32_t model_id, std::string model_name, db::DBContext& db );

} // namespace idhan::embeddings
