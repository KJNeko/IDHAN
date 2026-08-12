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
/** Upserts into embedding_models, which fires the trigger that creates that model's
 *  embeddings_<model_id> table at the model's own width.
 *
 *  A model already present with a DIFFERENT width is left alone and logged loudly rather than
 *  updated: the existing table's halfvec column cannot change width under rows that are already in
 *  it, so the honest outcome is to refuse the model until an operator intervenes. */
IDHANTask< void > registerEmbeddingModels( DbClientPtr db );

//! The job that fills in every missing embedding for one model.
/** Sweeps records that have no row in embeddings_<model_id> yet, skipping any whose MIME nothing can
 *  decode, and works through them with several calls in flight at once.
 *
 *  Idempotent by construction: the per-model table is keyed by record_id and written with ON
 *  CONFLICT DO UPDATE, so an interrupted run is resumed simply by starting another. */
JobTask backfillJob( std::int32_t model_id, std::string model_name );

//! Claims the right to run a backfill for \p model_id.
/** \return false when one is already running for that model. Two concurrent backfills would both see
 *  the same "not embedded yet" set and do every record twice.
 *
 *  The claim is released by backfillJob when it finishes, however it finishes -- which is why the
 *  set lives beside the job rather than beside the endpoint that calls this. */
[[nodiscard]] bool tryBeginBackfill( std::int32_t model_id );

//! Releases a claim taken by tryBeginBackfill without a job having run.
/** Only for a caller that claimed a model to keep a backfill out while it did something else --
 *  deleting the model, in practice. A backfill releases its own claim through the guard in its
 *  coroutine frame and must never call this. */
void endBackfill( std::int32_t model_id );

} // namespace idhan::embeddings
