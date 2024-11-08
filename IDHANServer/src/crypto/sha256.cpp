//
// Created by kj16609 on 7/30/24.
//

#include "sha256.hpp"

#include <QCryptographicHash>

#include <openssl/evp.h>

#include <cassert>
#include <cstring>
#include <vector>

namespace idhan
{
SHA256::SHA256( const std::byte* data ) : m_data()
{
	std::memcpy( m_data.data(), data, m_data.size() );
}

QString SHA256::hex() const
{
	return QString( m_data.toHex() );
}

SHA256::SHA256( QIODevice* io ) : m_data()
{
	QCryptographicHash hasher { QCryptographicHash::Sha256 };
	hasher.addData( io );

	m_data = hasher.result();
}

} // namespace idhan