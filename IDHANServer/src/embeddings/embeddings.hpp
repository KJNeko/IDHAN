#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "jobs/JobTask.hpp"
#include "threading/IDHANTask.hpp"

namespace idhan::embeddings
{

struct EmbeddingModelInfo
{
	std::int32_t id;
	std::int32_t dimensions;
};

[[nodiscard]] IDHANTask< std::optional< EmbeddingModelInfo > > findEmbeddingModel(
	std::string_view model_name,
	DbClientPtr db );

IDHANTask< void > registerEmbeddingModels( DbClientPtr db );

JobTask backfillJob( std::int32_t model_id, std::string model_name );

//! False when one is already running for that model. The claim is released by backfillJob however it
//! finishes, so the set lives beside the job rather than beside the endpoint.
[[nodiscard]] bool tryBeginBackfill( std::int32_t model_id );

void endBackfill( std::int32_t model_id );

} // namespace idhan::embeddings
