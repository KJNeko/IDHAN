#pragma once

#include <cstddef>
#include <meta>
#include <string_view>

namespace idhan::http
{

//! Annotation values must be structural. Promote text to static storage so the
//! public pointer is valid in annotation constants and views outlive extraction.
struct AnnotationText
{
	const char* characters;
	std::size_t length;

	consteval AnnotationText( const std::string_view text = {} ) :
	  characters { std::define_static_string( text ) },
	  length { text.size() }
	{}

	template < std::size_t N >
	consteval AnnotationText( const char ( &text )[ N ] ) : AnnotationText { std::string_view { text, N - 1 } }
	{}

	[[nodiscard]] constexpr std::string_view view() const { return { characters, length }; }
};

} // namespace idhan::http
