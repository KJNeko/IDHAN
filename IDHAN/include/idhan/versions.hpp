#pragma once

//! Packs a semantic version into a single int as (major << 16) | (minor << 8) | patch.
#define MAKE_IDHAN_VERSION( major, minor, patch ) ( ( major << 16 ) | ( minor << 8 ) | patch )

#ifndef IDHAN_MAJOR_VERSION
#error Major version must be specified for release builds
#endif

#ifndef IDHAN_MINOR_VERSION
#error Minor version must be specified for release builds
#endif

#ifndef IDHAN_PATCH_VERSION
#error Patch version must be specified for release builds
#endif

//! The compiled-in IDHAN version, packed via MAKE_IDHAN_VERSION. The IDHAN_{MAJOR,MINOR,PATCH}_VERSION
//! macros are supplied by the build system and are required (the #errors above enforce this).
#define IDHAN_VERSION MAKE_IDHAN_VERSION( IDHAN_MAJOR_VERSION, IDHAN_MINOR_VERSION, IDHAN_PATCH_VERSION )

//! Wall-clock time of this translation unit's compilation (__TIME__).
#define IDHAN_BUILD_TIME __TIME__
//! Calendar date of this translation unit's compilation (__DATE__).
#define IDHAN_BUILD_DATE __DATE__