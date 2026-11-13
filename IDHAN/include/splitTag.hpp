#pragma once

#include <string>

namespace idhan
{

constexpr char TAG_NAMESPACE_DELIMTER { ':' };

//! Returns {namespace, subtag}, splitting at the first ':'. No delimiter means an empty namespace.
std::pair< std::string, std::string > splitTag( const std::string_view str );

//! \copydoc splitTag(const std::string_view)
inline std::pair< std::string, std::string > splitTag( const std::string& str )
{
	const std::string_view view { str };
	return splitTag( view );
}

namespace tags
{

//! \copydoc idhan::splitTag(const std::string_view)
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
