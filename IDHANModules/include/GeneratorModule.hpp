#pragma once
#include <json/value.h>

#include <expected>

#include "IDHANTypes.hpp"
#include "ModuleBase.hpp"
#include "ModuleSink.hpp"

namespace idhan
{

class FGL_EXPORT GeneratorModuleI : public ModuleBase
{
  public:

	GeneratorModuleI() = delete;

	GeneratorModuleI( ModuleCallbacks callbacks ) : ModuleBase( callbacks ) {}

	~GeneratorModuleI() override;

	[[nodiscard]] virtual std::vector< MimeID > handleableMimes() = 0;

	//! Produces a derived file (e.g. extracting a specific member from an archive).
	//! \param desired_hash SHA-256 of the derived file the caller wants; the module locates and
	//!        writes the matching output.
	//! \param out Where the derived file is written (see ModuleSink). Nothing is written when the
	//!        module returns an error.
	[[nodiscard]] virtual std::expected< void, idhan::ModuleError > generate(
		ModuleCallData& data,
		std::array< std::byte, 256 / 8 > desired_hash,
		ModuleSink& out ) = 0;

	[[nodiscard]] bool canHandle( MimeID mime_id );

	ModuleType type() override;
};

} // namespace idhan