//
// Created by kj16609 on 8/11/24.
//

#pragma once

#include <QString>

#include <format>

template <>
struct std::formatter< QString >
{
	template < class ParseContext >
	constexpr typename ParseContext::iterator parse( ParseContext& ctx )
	{
		return ctx.begin();
	}

	template < class FmtContext >
	typename FmtContext::iterator format( const QString s, FmtContext& ctx ) const
	{
		return std::format_to( ctx.out(), "{}", s.toStdString() );
	}
};
