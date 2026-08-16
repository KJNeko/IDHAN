#pragma once

#include <QString>

#include "logging/format_ns.hpp"

//! format_ns/{fmt} formatter specialization that lets a QString be passed directly to
//! format_ns::format; it is rendered via QString::toStdString().
template <>
struct format_ns::formatter< QString >
{
	template < class ParseContext >
	constexpr typename ParseContext::iterator parse( ParseContext& ctx )
	{
		return ctx.begin();
	}

	template < class FmtContext >
	typename FmtContext::iterator format( const QString s, FmtContext& ctx ) const
	{
		return format_ns::format_to( ctx.out(), "{}", s.toStdString() );
	}
};
