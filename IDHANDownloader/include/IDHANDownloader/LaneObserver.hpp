#pragma once

#include "LaneSnapshot.hpp"

namespace idhan::downloader
{

//! Called from downloader IO threads; implementations must be thread-safe and quick.
class LaneObserver
{
  public:

	virtual ~LaneObserver() = default;

	virtual void onLaneChanged( const LaneSnapshot& snapshot ) = 0;
};

} // namespace idhan::downloader
