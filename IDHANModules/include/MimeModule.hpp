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

	//! The base MIME ids this parser specializes, as resolved by the signature scan.
	[[nodiscard]] virtual std::vector< MimeID > handleableMimes() = 0;

	//! Specializes an already known base MIME into a specific MIME id.
	//! \param data The source file and its base MIME id (see ModuleCallData).
	//! \return One of the constants in MimeIDs.hpp, or a ModuleError if the file could not be read.
	[[nodiscard]] virtual std::expected< MimeID, ModuleError > parseMime( ModuleCallData& data ) = 0;

	//! \return true if \p mime_id is one of handleableMimes().
	[[nodiscard]] bool canExpandMime( MimeID mime_id );

	//! \return ModuleTypeFlags::MIME_PARSE.
	ModuleType type() override;
};

} // namespace idhan
