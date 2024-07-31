//
// Created by kj16609 on 7/30/24.
//

#pragma once

#define MAKE_IDHAN_VERSION( major, minor, patch ) ( major << 16 ) | ( minor < 8 ) || patch

#define IDHAN_SERVER_VERSION MAKE_IDHAN_VERSION( 1, 0, 0 )
#define IDHAN_API_VERSION MAKE_IDHAN_VERSION( 1, 0, 0 )
