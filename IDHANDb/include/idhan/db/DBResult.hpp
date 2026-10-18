#pragma once
#include <cstddef>
#include <iterator>
#include <libpq-fe.h>
#include <memory>
#include <string>
#include <tuple>

#include "DBError.hpp"

namespace idhan::db
{
struct PGResultDeleter
{
	void operator()( PGresult* ) const noexcept;
};

using DBResult = std::unique_ptr< PGresult, PGResultDeleter >;

template < typename... Ts >
class DBRows
{
	DBResult m_result;
	std::string m_sql;
	[[nodiscard]] DBDecodeError contextualError( const DBDecodeError&, std::optional< std::size_t > row = {} ) const;

  public:

	explicit DBRows( DBResult, std::string sql = {} );
	DBRows( DBRows&& ) noexcept;
	DBRows& operator=( DBRows&& ) noexcept;
	DBRows( const DBRows& ) = delete;
	DBRows& operator=( const DBRows& ) = delete;
	~DBRows();
	[[nodiscard]] std::size_t size() const noexcept;
	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] std::tuple< Ts... > row( std::size_t index ) const;
	// Borrowed field views live only as long as this result.
	[[nodiscard]] PGresult* get() const noexcept;
	[[nodiscard]] DBResult release() &&;

	class Iterator
	{
		const DBRows* m_rows;
		std::size_t m_index;
		friend class DBRows;
		Iterator( const DBRows*, std::size_t );

	  public:

		using value_type = std::tuple< Ts... >;
		using difference_type = std::ptrdiff_t;
		using iterator_category = std::input_iterator_tag;
		Iterator();
		[[nodiscard]] value_type operator*() const;
		Iterator& operator++();
		Iterator operator++( int );
		[[nodiscard]] bool operator==( const Iterator& ) const;
	};

	[[nodiscard]] Iterator begin() const;
	[[nodiscard]] Iterator end() const;
};
} // namespace idhan::db

#include "detail/DBResult-inl.hpp"
