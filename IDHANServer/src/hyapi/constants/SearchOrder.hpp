//
// Created by kj16609 on 11/2/24.
//

#pragma once

namespace idhan::hyapi
{

enum SearchOrder
{
	FileSize = 0,
	Duration = 1,
	ImportTime = 2,
	FileType = 3,
	Random = 4,
	Width = 5,
	Height = 6,
	Ratio = 7,
	NumberOfPixels = 8,
	NumberOfTags = 9,
	NumberOfMediaViews = 10,
	TotalMediaViewtime = 11,
	ApproximateBitrate = 12,
	HasAudio = 13,
	ModifiedTime = 14,
	Framerate = 15,
	NumberOfFrames = 16,
	// 17 UNUSED
	LastViewedTime = 18,
	ArchiveTimestamp = 19,
	HashHex = 20,
	PixelHashHex = 21,
	BlurHash = 22,
};

} // namespace idhan::hyapi
