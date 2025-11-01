//
// Created by kj16609 on 7/30/24.
//

#pragma once

#define MAKE_IDHAN_VERSION( major, minor, patch ) int( ( major << 16 ) | ( minor < 8 ) || patch )

#define IDHAN_API_MAJOR 0
#define IDHAN_API_MINOR 0
#define IDHAN_API_PATCH 0

#define IDHAN_SERVER_MAJOR 0
#define IDHAN_SERVER_MINOR 0
#define IDHAN_SERVER_PATCH 0

#ifdef NDEBUG

#ifndef IDHAN_SERVER_MAJOR
#error Major version must be specified for release builds
#endif

#ifndef IDHAN_SERVER_MINOR
#error Minor version must be specified for release builds
#endif

#ifndef IDHAN_SERVER_PATCH
#error Patch version must be specified for release builds
#endif

#ifndef IDHAN_API_MAJOR
#error Major version must be specified for release builds
#endif

#ifndef IDHAN_API_MINOR
#error Minor version must be specified for release builds
#endif

#ifndef IDHAN_API_PATCH
#error Patch version must be specified for release builds
#endif

#else
#ifndef IDHAN_SERVER_MAJOR
#define IDHAN_SERVER_MAJOR 0
#endif

#ifndef IDHAN_SERVER_MINOR
#define IDHAN_SERVER_MINOR 0
#endif

#ifndef IDHAN_SERVER_PATCH
#define IDHAN_SERVER_PATCH 0
#endif

#ifndef IDHAN_API_MAJOR
#define IDHAN_API_MAJOR 0
#endif

#ifndef IDHAN_API_MINOR
#define IDHAN_API_MINOR 0
#endif

#ifndef IDHAN_API_PATCH
#define IDHAN_API_PATCH 0
#endif
#endif

#define IDHAN_SERVER_VERSION MAKE_IDHAN_VERSION( IDHAN_SERVER_MAJOR, IDHAN_SERVER_MINOR, IDHAN_SERVER_PATCH )

#define IDHAN_API_VERSION MAKE_IDHAN_VERSION( IDHAN_API_MAJOR, IDHAN_API_MINOR, IDHAN_API_PATCH )
