//
// Created by kj16609 on 11/24/25.
//
#pragma once
#include <json/value.h>

#include <expected>

#include "IDHANTypes.hpp"
#include "ModuleBase.hpp"

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
	//!        returns the matching output.
	//! \return The derived file's bytes, or a ModuleError if it could not be produced.
	[[nodiscard]] virtual std::expected< std::vector< std::byte >, idhan::ModuleError > generate(
		ModuleCallData& data,
		std::array< std::byte, 256 / 8 > desired_hash ) = 0;

	//! \return true if \p mime is one of handleableMimes().
	[[nodiscard]] bool canHandle( std::string_view mime );

	//! \return ModuleTypeFlags::GENERATOR.
	ModuleType type() override;
};

} // namespace idhan