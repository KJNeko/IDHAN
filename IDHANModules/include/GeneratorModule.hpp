//
// Created by kj16609 on 11/24/25.
//
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

	//! The canonical MIME types this generator can produce derived files from.
	[[nodiscard]] virtual std::vector< std::string_view > handleableMimes() = 0;

	//! Produces a derived file (e.g. extracting a specific member from an archive).
	//! \param data The source file and its MIME (see ModuleCallData).
	//! \param desired_hash SHA-256 of the derived file the caller wants; the module locates and
	//!        writes the matching output.
	//! \param out Where the derived file is written (see ModuleSink). Nothing is written when the
	//!        module returns an error.
	//! \return Nothing on success, or a ModuleError if the file could not be produced.
	/** Writing through a sink rather than returning a buffer is what keeps a large output from
	 *  existing twice. Reserve the size on \p out first where it is known -- an archive entry's
	 *  header usually carries it -- so the destination is allocated once. */
	[[nodiscard]] virtual std::expected< void, idhan::ModuleError > generate(
		ModuleCallData& data,
		std::array< std::byte, 256 / 8 > desired_hash,
		ModuleSink& out ) = 0;

	//! \return true if \p mime is one of handleableMimes().
	[[nodiscard]] bool canHandle( std::string_view mime );

	//! \return ModuleTypeFlags::GENERATOR.
	ModuleType type() override;
};

} // namespace idhan