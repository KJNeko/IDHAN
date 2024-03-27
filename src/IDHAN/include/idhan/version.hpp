//
// Created by kj16609 on 2/21/24.
//

#pragma once

#include <string_view>

#define IDHAN_MAJOR_VERSION 0
#define IDHAN_MINOR_VERSION 1
#define IDHAN_PATCH_VERSION 0

// Define version number as a string
#define STRINGIFY( x ) #x
#define TO_STRING( x ) STRINGIFY( x )

#define D_IDHAN_VERSION_STRING                                                                                         \
	TO_STRING( IDHAN_MAJOR_VERSION ) "." TO_STRING( IDHAN_MINOR_VERSION ) "." TO_STRING( IDHAN_PATCH_VERSION )

// 0.0.0

// 0000 0000 0000
// major minor patch

#define D_IDHAN_VERSION_NUMBER( MAJOR, MINOR, PATCH ) ( MAJOR * 10000 + MINOR * 100 + PATCH )

namespace idhan::version
{
	constexpr int IDHAN_VERSION {
		D_IDHAN_VERSION_NUMBER( IDHAN_MAJOR_VERSION, IDHAN_MINOR_VERSION, IDHAN_PATCH_VERSION )
	};

	constexpr static char IDHAN_VERSION_STRING[] { D_IDHAN_VERSION_STRING };

	constexpr std::string_view IDHAN_STRING_VIEW {
		std::string_view( IDHAN_VERSION_STRING, sizeof( IDHAN_VERSION_STRING ) - 1 )
	};

} // namespace idhan::version
