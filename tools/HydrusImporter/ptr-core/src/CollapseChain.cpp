#include "ptr/flatten/CollapseChain.hpp"

namespace idhan::hydrus::ptr
{

std::optional< CollapsedOp > collapseChain( const std::span< const MappingEvent > events )
{
	if ( events.empty() ) return std::nullopt;

	const auto& last = events.back();
	if ( static_cast< EventOp >( last.op ) == EventOp::Delete )
		return CollapsedOp { EventOp::Delete, last.update_index };

	for ( const auto& event : events )
	{
		if ( static_cast< EventOp >( event.op ) == EventOp::Add )
			return CollapsedOp { EventOp::Add, event.update_index };
	}

	// Unreachable: the last event is an add, so the loop above always finds one.
	return std::nullopt;
}

} // namespace idhan::hydrus::ptr
