//
// Created by kj16609 on 6/23/23.
//

#pragma once

#include <mutex>
#include <string>
#include <string_view>

#include "Binder.hpp"

namespace idhan::hydrus
{

	struct TransactionBase
	{
		TransactionBase() = delete;
		TransactionBase( sqlite3* ptr );

		TransactionBase( const TransactionBase& ) = delete;
		TransactionBase( TransactionBase&& ) = delete;
		TransactionBase& operator=( const TransactionBase& other ) = delete;

		bool m_finished { false };
		sqlite3* sqlite_db;
		std::mutex self_mtx {};

		inline Binder operator<<( std::string_view sql ) { return { sqlite_db, sql }; }

		template < std::uint64_t size >
		inline Binder operator<<( const char ( &raw_str )[ size - 1 ] )
		{
			const std::string_view str_view { std::string_view( raw_str, size - 1 ) };
			return *this << str_view;
		}
	};

	inline TransactionBase::TransactionBase( sqlite3* ptr ) : sqlite_db( ptr )
	{}

} // namespace idhan::hydrus
