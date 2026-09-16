#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

#include "IDHANTypes.hpp"
#include "modules/CallInput.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::mime
{

//! Specializes \p base_id using modules that can distinguish variants sharing one MIME string.
//! \return The specialized id, or \p base_id when nothing specializes it.
[[nodiscard]] runtime::Task< MimeID > specializeMimeID(
	MimeID base_id,
	std::shared_ptr< const modules::CallInput > input,
	std::string_view filename = {} );

[[nodiscard]] runtime::Task< MimeID > specializeMimeIDForPath( MimeID base_id, std::filesystem::path path );

} // namespace idhan::mime
