//
// Created by kj16609 on 7/24/24.
//

#pragma once

#include <QByteArray>
#include <QVarLengthArray>

#include <array>
#include <istream>

class QIODevice;

namespace idhan
{

class SHA256
{
	QByteArray m_data;

	SHA256() = delete;

	explicit SHA256( const std::byte* data );

	friend SHA256 createFromIStream( std::istream& istream );

  public:

	QString hex() const;

	explicit SHA256( QIODevice* io );
};

} // namespace idhan
