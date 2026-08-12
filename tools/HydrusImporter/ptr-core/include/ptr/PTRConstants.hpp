#pragma once

#include <cstdint>

namespace idhan::hydrus::ptr
{

// Serialisable types (from HydrusSerialisable.py)
inline constexpr int HYDRUS_TYPE_DICTIONARY { 21 };
inline constexpr int SERIALISABLE_TYPE_CONTENT_UPDATE { 34 };
inline constexpr int SERIALISABLE_TYPE_DEFINITIONS_UPDATE { 36 };
inline constexpr int SERIALISABLE_TYPE_METADATA { 37 };

// Content types (from HydrusConstants.py)
inline constexpr int CONTENT_TYPE_MAPPINGS { 0 };
inline constexpr int CONTENT_TYPE_TAG_SIBLINGS { 1 };
inline constexpr int CONTENT_TYPE_TAG_PARENTS { 2 };
inline constexpr int CONTENT_TYPE_FILES { 3 };
inline constexpr int CONTENT_TYPE_DEFINITIONS { 21 };

// Content update actions (from HydrusConstants.py)
inline constexpr int CONTENT_UPDATE_ADD { 0 };
inline constexpr int CONTENT_UPDATE_DELETE { 1 };
inline constexpr int CONTENT_UPDATE_PEND { 2 };
inline constexpr int CONTENT_UPDATE_RESCIND_PEND { 3 };
inline constexpr int CONTENT_UPDATE_PETITION { 4 };
inline constexpr int CONTENT_UPDATE_RESCIND_PETITION { 5 };

// Definitions types (from HydrusNetwork.py)
inline constexpr int DEFINITIONS_TYPE_HASHES { 0 };
inline constexpr int DEFINITIONS_TYPE_TAGS { 1 };

} // namespace idhan::hydrus::ptr
