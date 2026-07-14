//
// Created by kj16609 on 11/14/24.
//
#pragma once

#include <string>

namespace idhan
{

//! Character separating a tag's namespace from its subtag (the ':' in "creator:foo").
constexpr char TAG_NAMESPACE_DELIMTER { ':' };

//! Splits a "namespace:subtag" tag into its two parts.
//! \param str Full tag; only the first delimiter splits, so any later ':' stays in the subtag.
//! \return {namespace, subtag}; the namespace is empty when no delimiter is present.
std::pair< std::string, std::string > splitTag( const std::string_view str );

//! \copydoc splitTag(const std::string_view)
inline std::pair< std::string, std::string > splitTag( const std::string& str )
{
	const std::string_view view { str };
	return splitTag( view );
}

namespace tags
{

//! Convenience alias for idhan::splitTag; splits "namespace:subtag" into {namespace, subtag}.
inline std::pair< std::string, std::string > split( const std::string_view str )
{
	return splitTag( str );
}

//! \copydoc idhan::tags::split(const std::string_view)
inline std::pair< std::string, std::string > split( const std::string& str )
{
	const std::string_view view { str };
	return splitTag( view );
}

} // namespace tags

} // namespace idhan
