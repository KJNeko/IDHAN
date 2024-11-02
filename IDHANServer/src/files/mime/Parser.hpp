//
// Created by kj16609 on 8/11/24.
//

#pragma once
#include <QIODevice>

#include <cstddef>

namespace idhan::mime
{

	using MimeID = std::uint32_t;
	constexpr MimeID UNKNOWN_MIME_ID { std::numeric_limits< MimeID >::max() };

	void loadParsers();

	MimeID parse( QIODevice* io );

	MimeID getMimeFromIO( QIODevice* io );

} // namespace idhan::mime