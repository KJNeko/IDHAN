//
// Created by kj16609 on 7/30/24.
//

#pragma once

#define MAKE_IDHAN_VERSION( major, minor, patch ) int( ( major << 16 ) | ( minor < 8 ) | patch )

#ifndef IDHAN_MAJOR_VERSION
#error Major version must be specified for release builds
#endif

#ifndef IDHAN_MINOR_VERSION
#error Minor version must be specified for release builds
#endif

#ifndef IDHAN_PATCH_VERSION
#error Patch version must be specified for release builds
#endif

#define IDHAN_VERSION MAKE_IDHAN_VERSION( IDHAN_MAJOR_VERSION, IDHAN_MINOR_VERSION, IDHAN_PATCH_VERSION )

#define IDHAN_BUILD_TIME __TIME__
#define IDHAN_BUILD_DATE __DATE__