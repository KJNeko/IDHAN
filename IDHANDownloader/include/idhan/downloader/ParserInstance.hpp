#pragma once

#include <memory>

#include "IDHANJSRuntime.hpp"
#include "JSReturn.hpp"
#include "ParserScript.hpp"
#include "manifest/ParserManifest.hpp"

class JSModule
{
	JSReturn m_module;
	JSReturn m_namespace;
	std::string m_filename;

	[[nodiscard]] std::string errorMessage( const char* operation, const JSReturn& exception ) const;
	void resolveImports() const;
	void evaluate() const;
	void loadNamespace();

  public:

	explicit JSModule( JSReturn module, std::string filename );

	[[nodiscard]] JSReturn getExport( const char* name ) const;
	// Missing exports return JS_UNDEFINED; JavaScript access errors still throw.
	[[nodiscard]] JSReturn findExport( const char* name ) const;
};

class ParserInstance
{
	std::shared_ptr< CompiledScript > m_script {};
	std::shared_ptr< IDHANJSRuntime > m_runtime;
	JSModule m_module;

	[[nodiscard]] JSReturn loadBytecode() const;

	explicit ParserInstance( const std::shared_ptr< CompiledScript >& script );

	friend class CompiledScript;

  public:

	[[nodiscard]] ParserManifest manifest();
	[[nodiscard]] std::vector< URLClass > urlClasses() const;
	[[nodiscard]] bool hasManifest() const;
	[[nodiscard]] bool hasUrlClass() const;
};
