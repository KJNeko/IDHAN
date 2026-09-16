#include "idhan/db/DBBindings.hpp"

namespace idhan::db
{
DBBindings::DBBindings() = default;

void DBBindings::append( DBParameter value )
{
	if ( m_values.size() >= INT_MAX ) throw std::length_error( "Too many PostgreSQL parameters" );

	if ( value.oid == 0 ) throw std::invalid_argument( "A binding must specify its OID" );

	if ( value.format != ResultFormat::binary && value.format != ResultFormat::text )
		throw std::invalid_argument( "Invalid binding format" );

	if ( value.binary && value.text ) throw std::invalid_argument( "Binding has both text and binary storage" );

	if ( value.binary && value.format != ResultFormat::binary )
		throw std::invalid_argument( "Binary binding format mismatch" );

	if ( value.text && value.format != ResultFormat::text )
		throw std::invalid_argument( "Text binding format mismatch" );

	if ( value.text && value.text->contains( '\0' ) ) throw std::invalid_argument( "Text binding contains NUL" );
	detail::checkLength( detail::parameterBytes( value ).size() );
	m_values.push_back( std::move( value ) );
}

std::span< const DBParameter > DBBindings::values() const noexcept
{
	return m_values;
}

std::vector< Oid > DBBindings::oids() const
{
	std::vector< Oid > result;
	result.reserve( m_values.size() );
	for ( const auto& value : m_values ) result.push_back( value.oid );
	return result;
}

std::vector< const char* > DBBindings::pointers() const
{
	static constexpr char empty { '\0' };
	std::vector< const char* > result;
	result.reserve( m_values.size() );
	for ( const auto& value : m_values )
	{
		if ( value.text )
			result.push_back( value.text->c_str() );
		else if ( value.binary )
			result.push_back(
				value.binary->empty() ? &empty : reinterpret_cast< const char* >( value.binary->data() ) );
		else
			result.push_back( nullptr );
	}
	return result;
}

std::vector< int > DBBindings::lengths() const
{
	std::vector< int > result;
	result.reserve( m_values.size() );
	for ( const auto& value : m_values )
		result.push_back( static_cast< int >( detail::parameterBytes( value ).size() ) );
	return result;
}

std::vector< int > DBBindings::formats() const
{
	std::vector< int > result;
	result.reserve( m_values.size() );
	for ( const auto& value : m_values ) result.push_back( static_cast< int >( value.format ) );
	return result;
}
} // namespace idhan::db
