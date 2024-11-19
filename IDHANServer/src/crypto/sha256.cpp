//
// Created by kj16609 on 7/30/24.
//

#include "sha256.hpp"

#include <QCryptographicHash>

#include <openssl/evp.h>

#include <cassert>
#include <cstring>
#include <vector>

#include "drogon/orm/Field.h"

namespace idhan
{

SHA256::SHA256( const std::byte* data ) : m_data()
{
	std::memcpy( m_data.data(), data, m_data.size() );
}

SHA256::SHA256( const drogon::orm::Field& field )
{
	const auto data { field.as< std::vector< char > >() };

	assert( data.size() == m_data.size() );

	std::memcpy( m_data.data(), data.data(), data.size() );
}

std::string SHA256::hex() const
{
	std::string str {};
	str.reserve( m_data.size() );
	for ( std::size_t i = 0; i < m_data.size(); ++i )
		str += std::format( "{:02x}", static_cast< std::uint8_t >( m_data[ i ] ) );
	return str;
}

SHA256::SHA256( QIODevice* io ) : m_data()
{
	QCryptographicHash hasher { QCryptographicHash::Sha256 };
	hasher.addData( io );

	const auto result { hasher.result() };
	std::memcpy( m_data.data(), result.data(), result.size() );
}

} // namespace idhan