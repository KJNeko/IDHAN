#include "ptr/flatten/CollapseChain.hpp"

namespace idhan::hydrus::ptr
{

std::optional< CollapsedOp > collapseChain( const std::span< const MappingEvent > events )
{
	if ( events.empty() ) return std::nullopt;

	// The chain's final state is whatever the last event said. Everything before it is
	// superseded -- that is the entire point of flattening.
	const auto& last = events.back();
	if ( static_cast< EventOp >( last.op ) == EventOp::Delete )
		return CollapsedOp { EventOp::Delete, last.update_index };

	// Terminal add: attribute it to the first add, so a mapping that was deleted and re-added
	// reads as having existed since it was originally applied.
	for ( const auto& event : events )
	{
		if ( static_cast< EventOp >( event.op ) == EventOp::Add )
			return CollapsedOp { EventOp::Add, event.update_index };
	}

	// Unreachable: the last event is an add, so the loop above always finds one.
	return std::nullopt;
}

} // namespace idhan::hydrus::ptr
