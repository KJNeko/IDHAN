#pragma once
#include <expected>
#include <string_view>
#include <vector>

#include "IDHANTypes.hpp"
#include "ModuleBase.hpp"

namespace idhan
{

class FGL_EXPORT MimeModuleI : public ModuleBase
{
  public:

	MimeModuleI() = delete;

	MimeModuleI( ModuleCallbacks callbacks ) : ModuleBase( callbacks ) {}

	~MimeModuleI() override;

	//! The generic mime ids this parser refines, as resolved by the pre-scan.
	[[nodiscard]] virtual std::vector< MimeID > handleableMimes() = 0;

	//! Refines an already known generic mime into a specific mime id.
	//! \param data The source file and its generic mime id (see ModuleCallData).
	//! \return One of the constants in MimeIDs.hpp, or a ModuleError if the file could not be read.
	[[nodiscard]] virtual std::expected< MimeID, ModuleError > parseMime( ModuleCallData& data ) = 0;

	//! \return true if \p mime_id is one of handleableMimes().
	[[nodiscard]] bool canExpandMime( MimeID mime_id );

	//! \return ModuleTypeFlags::MIME_PARSE.
	ModuleType type() override;
};

} // namespace idhan
