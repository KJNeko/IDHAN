//
// Created by kj16609 on 7/24/24.
//

#pragma once

#include <QByteArray>
#include <QVarLengthArray>

#include <array>
#include <istream>

namespace drogon::orm
{
class Field;
}
class QIODevice;

namespace idhan
{

class SHA256
{
	std::array< std::byte, ( 256 / 8 ) > m_data {};

	SHA256() = delete;

	explicit SHA256( const std::byte* data );

	friend SHA256 createFromIStream( std::istream& istream );

  public:

	explicit SHA256( const drogon::orm::Field& field );

	std::string hex() const;

	explicit SHA256( QIODevice* io );
};

} // namespace idhan
