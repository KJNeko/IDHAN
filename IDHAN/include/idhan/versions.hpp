//
// Created by kj16609 on 7/30/24.
//

#pragma once

//! Packs a semantic version (major, minor, patch) into a single int.
//! \bug The minor term is written `minor < 8` (a comparison yielding 0 or 1) instead of
//!      `minor << 8`, so the minor version is not encoded into its byte. Left as-is here; fixing it
//!      changes the numeric value of IDHAN_VERSION and should be done deliberately.
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

//! The compiled-in IDHAN version, packed via MAKE_IDHAN_VERSION. The IDHAN_{MAJOR,MINOR,PATCH}_VERSION
//! macros are supplied by the build system and are required (the #errors above enforce this).
#define IDHAN_VERSION MAKE_IDHAN_VERSION( IDHAN_MAJOR_VERSION, IDHAN_MINOR_VERSION, IDHAN_PATCH_VERSION )

//! Wall-clock time of this translation unit's compilation (__TIME__).
#define IDHAN_BUILD_TIME __TIME__
//! Calendar date of this translation unit's compilation (__DATE__).
#define IDHAN_BUILD_DATE __DATE__