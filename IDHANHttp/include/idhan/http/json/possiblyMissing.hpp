#pragma once

#include <optional>
#include <utility>

namespace idhan::http
{

//! Uses std::optional's value API, but an empty object member is omitted from JSON.
//! Nest std::optional<T> as the value type when explicit JSON null is also valid.
template < typename T >
struct possiblyMissing : std::optional< T >
{
	using std::optional< T >::optional;
	using std::optional< T >::operator=;

	constexpr possiblyMissing() noexcept = default;

	explicit constexpr possiblyMissing( std::optional< T > value ) : std::optional< T > { std::move( value ) } {}
};

template < typename T >
possiblyMissing( T ) -> possiblyMissing< T >;

} // namespace idhan::http
