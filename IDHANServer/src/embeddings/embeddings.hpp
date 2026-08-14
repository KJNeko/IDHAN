#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "jobs/JobTask.hpp"
#include "threading/IDHANTask.hpp"

namespace idhan::embeddings
{

//! Registers every embedding model the loaded modules provide.
IDHANTask< void > registerEmbeddingModels( DbClientPtr db );

//! The job that fills in every missing embedding for one model.
JobTask backfillJob( std::int32_t model_id, std::string model_name );

//! Claims the right to run a backfill for \p model_id.
/** \return false when one is already running for that model. Two concurrent backfills would both see
 *  the same "not embedded yet" set and do every record twice.
 *
 *  The claim is released by backfillJob when it finishes, however it finishes -- which is why the
 *  set lives beside the job rather than beside the endpoint that calls this. */
[[nodiscard]] bool tryBeginBackfill( std::int32_t model_id );

//! Releases a claim taken by tryBeginBackfill without a job having run.
void endBackfill( std::int32_t model_id );

} // namespace idhan::embeddings
