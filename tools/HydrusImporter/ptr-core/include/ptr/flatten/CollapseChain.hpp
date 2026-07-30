#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "ptr/flatten/MappingEvent.hpp"

namespace idhan::hydrus::ptr
{

//! The single operation a whole add/delete chain reduces to.
struct CollapsedOp
{
	EventOp op;
	std::uint16_t update_index;
};

//! Reduces one key's history to at most one operation.
//!
//! A chain ending in a delete keeps that delete, at its own index. A chain ending in an add
//! keeps the *first* add in the chain, preserving original attribution across any number of
//! delete/re-add cycles.
//!
//! \param events All events for exactly one (hash_id, tag_id), sorted by eventLess.
//! \return The surviving operation, or nullopt if \p events is empty.
std::optional< CollapsedOp > collapseChain( std::span< const MappingEvent > events );

} // namespace idhan::hydrus::ptr
