#pragma once

#include <cstdint>

namespace idhan::downloader
{

//! Milestones of one request, reported once each in this order. STORING only occurs for imports.
enum class TransferEvent : std::uint8_t
{
	DOWNLOADING,
	STORING,
	FINISHED,
};

} // namespace idhan::downloader
