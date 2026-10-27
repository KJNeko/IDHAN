#pragma once
#include <cstddef>
#include <libpq-fe.h>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "DBConfig.hpp"
#include "Network.hpp"

namespace idhan::db
{
struct DBParameter
{
	Oid oid { 0 };
	ResultFormat format { ResultFormat::binary };
	std::optional< std::vector< std::byte > > binary {};
	std::optional< std::string > text {};
	bool secret { false };
};

template < typename T >
struct DBCodec
{
	[[nodiscard]] static constexpr Oid oid();
	[[nodiscard]] static DBParameter encode( const T& );
	[[nodiscard]] static T decode( std::span< const std::byte > );
	[[nodiscard]] static std::string diagnostic( const T&, std::size_t max_bytes );
};

template < typename T >
struct SecretParameter
{
	T value;
};

template < typename T >
[[nodiscard]] SecretParameter< T > secret( T value );

class DBBindings
{
	std::vector< DBParameter > m_values {};

  public:

	DBBindings();
	template < typename T >
	void bind( T&& value );
	void append( DBParameter );
	[[nodiscard]] std::span< const DBParameter > values() const noexcept;
	// Returned pointers borrow this binding set. Do not move or mutate it while
	// those pointers are in use (including appending another parameter).
	[[nodiscard]] std::vector< Oid > oids() const;
	[[nodiscard]] std::vector< const char* > pointers() const;
	[[nodiscard]] std::vector< int > lengths() const;
	[[nodiscard]] std::vector< int > formats() const;
};

} // namespace idhan::db

#include "detail/DBBindings-inl.hpp"
