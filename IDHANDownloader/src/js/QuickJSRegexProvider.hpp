#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <quickjs.h>
#include <string>
#include <string_view>
#include <vector>

namespace idhan::downloader::quickjs
{
//! Ada's provider interface is static. Scope selects the calling realm for
//! allocation and interruption without retaining a context in compiled patterns.
class QuickJSRegexProvider
{
  public:

	struct Bytecode
	{
		JSRuntime* runtime {};
		std::uint8_t* data {};

		Bytecode() = default;
		~Bytecode();

		Bytecode( const Bytecode& ) = delete;
		Bytecode& operator=( const Bytecode& ) = delete;
	};

	using regex_type = std::shared_ptr< Bytecode >;

	class Scope
	{
		JSContext* m_previous;

	  public:

		explicit Scope( JSContext* context ) noexcept;
		~Scope();

		Scope( const Scope& ) = delete;
		Scope& operator=( const Scope& ) = delete;
	};

	static std::optional< regex_type > create_instance( std::string_view pattern, bool ignore_case );

	static std::optional< std::vector< std::optional< std::string > > > regex_search(
		std::string_view input,
		const regex_type& pattern ) noexcept;

	static bool regex_match( std::string_view input, const regex_type& pattern ) noexcept;

  private:

	static thread_local JSContext* s_context;
};
} // namespace idhan::downloader::quickjs
