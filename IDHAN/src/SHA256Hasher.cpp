#include "idhan/SHA256Hasher.hpp"

#include <array>
#include <stdexcept>

namespace idhan
{

SHA256Hasher::SHA256Hasher()
{
	if ( !m_context || EVP_DigestInit_ex( m_context.get(), EVP_sha256(), nullptr ) != 1 )
		throw std::runtime_error( "Could not initialize SHA-256" );
}

void SHA256Hasher::update( const std::span< const std::byte > bytes )
{
	if ( m_finished ) throw std::logic_error( "SHA-256 hasher is already finalized" );

	if ( EVP_DigestUpdate( m_context.get(), bytes.data(), bytes.size() ) != 1 )
		throw std::runtime_error( "Could not update SHA-256" );
}

SHA256 SHA256Hasher::finish()
{
	if ( m_finished ) throw std::logic_error( "SHA-256 hasher is already finalized" );

	std::array< std::byte, SHA256::size() > bytes {};
	unsigned int length {};
	if ( EVP_DigestFinal_ex( m_context.get(), reinterpret_cast< unsigned char* >( bytes.data() ), &length ) != 1
	     || length != bytes.size() )
		throw std::runtime_error( "Could not finish SHA-256" );

	m_finished = true;
	return SHA256::fromBuffer( bytes );
}

} // namespace idhan
