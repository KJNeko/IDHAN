#pragma once

#include <filesystem>
#include <memory>

#include "IDHANTypes.hpp"
#include "modules/CallInput.hpp"
#include "threading/IDHANTask.hpp"

namespace idhan::mime
{

//! Expands \p generic_id using whichever modules refine it. An expanded id reports the same mime
//! string as the generic one, so only the id changes.
//! \return The expanded id, or \p generic_id when nothing expands it.
[[nodiscard]] IDHANTask< MimeID > refineMimeID( MimeID generic_id, std::shared_ptr< const modules::CallInput > input );

//! refineMimeID against a file on disk. Returns \p generic_id if the file cannot be opened.
[[nodiscard]] IDHANTask< MimeID > refineMimeIDForPath( MimeID generic_id, std::filesystem::path path );

} // namespace idhan::mime
