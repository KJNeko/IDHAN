#pragma once

#include <string_view>

#include "IDHANTypes.hpp"

namespace idhan::mime
{

//! Narrows \p mime_id using \p filename where the bytes of two types are identical. A comic book
//! zip is a zip, so nothing in a signature scan can tell them apart.
//! \return The narrowed id, or \p mime_id when no guess applies.
[[nodiscard]] MimeID guessMimeFromExtension( MimeID mime_id, std::string_view filename );

} // namespace idhan::mime
