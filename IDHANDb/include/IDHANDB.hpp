#pragma once

#include <array>
#include <bit>
#include <chrono>
#include <climits>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <libpq-fe.h>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "idhan/db/DBResult.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{

template < typename T >
struct strip_optional_impl
{
	using type = T;
};

template < typename T >
struct strip_optional_impl< std::optional< T > >
{
	using type = T;
};

template < typename T >
using strip_optional = typename strip_optional_impl< std::remove_cvref_t< T > >::type;

template < typename T >
inline constexpr bool is_optional { false };

template < typename T >
inline constexpr bool is_optional< std::optional< T > > { true };

template < typename T >
inline constexpr bool is_vector { false };

template < typename T, typename TAllocator >
inline constexpr bool is_vector< std::vector< T, TAllocator > > { true };

class DBOperation;

enum class DBTaskState
{
	NEEDS_READ,
	NEEDS_WRITE,
	RESULT_READY
};

class DBTask
{
	PGconn* m_conn { nullptr };
	std::shared_ptr< DBOperation > m_operation;

	friend class IDHANPQContext;
	friend class DBContext;
	friend class DBTransaction;

	explicit DBTask( PGconn* conn ) : m_conn( conn ) {}

	[[nodiscard]] std::runtime_error connectionError( std::string_view operation ) const;

  public:

	// Replacement tasks own cold operation state; the PGconn API below is the legacy prototype.
	explicit DBTask( std::shared_ptr< DBOperation > );

	class Awaiter
	{
		std::shared_ptr< DBOperation > m_operation;
		friend class DBTask;
		explicit Awaiter( std::shared_ptr< DBOperation > );

	  public:

		~Awaiter();
		Awaiter( Awaiter&& ) noexcept;
		Awaiter& operator=( Awaiter&& ) noexcept;
		Awaiter( const Awaiter& ) = delete;
		Awaiter& operator=( const Awaiter& ) = delete;
		[[nodiscard]] bool await_ready() const noexcept;
		void await_suspend( std::coroutine_handle<> );
		[[nodiscard]] DBResult await_resume();
	};

	[[nodiscard]] Awaiter operator co_await() &;
	[[nodiscard]] Awaiter operator co_await() &&;
	[[nodiscard]] runtime::Task< DBResult > result();
	template < typename... Ts >
	[[nodiscard]] runtime::Task< DBRows< Ts... > > rows();
	template < typename... Ts >
	[[nodiscard]] runtime::Task< std::optional< std::tuple< Ts... > > > nextRow();
	void abandon() noexcept;

	DBTask() = delete;
	DBTask( const DBTask& ) = delete;
	DBTask& operator=( const DBTask& ) = delete;
	DBTask( DBTask&& ) noexcept = default;
	DBTask& operator=( DBTask&& ) noexcept = default;

	[[nodiscard]] int socket() const;
	[[nodiscard]] DBTaskState poll();
	[[nodiscard]] DBResult nextResult();
};

template < typename T >
constexpr Oid defineParamType()
{
	return DBCodec< std::decay_t< T > >::oid();
}

template < typename... TArgs >
constexpr std::array< Oid, sizeof...( TArgs ) > defineParamTypes()
{
	return { { defineParamType< TArgs >()... } };
}

static_assert( defineParamTypes< int, int, int >().size() == 3 );
static_assert( defineParamTypes< int, int, int >()[ 0 ] == 23 );
static_assert( defineParamTypes< int, int, int >()[ 1 ] == 23 );
static_assert( defineParamTypes< int, int, int >()[ 2 ] == 23 );

struct ParamValue
{
	std::string m_text {};
	std::vector< std::byte > m_binary {};
	bool m_is_null { false };
	int m_format { 1 };

	[[nodiscard]] const char* data() const
	{
		if ( m_is_null ) return nullptr;

		if ( m_format == 0 ) return m_text.c_str();

		static constexpr char empty { '\0' };
		if ( m_binary.empty() ) return &empty;

		return reinterpret_cast< const char* >( m_binary.data() );
	}

	[[nodiscard]] int size() const
	{
		const std::size_t size { m_format == 0 ? m_text.size() : m_binary.size() };
		if ( size > INT_MAX ) throw std::length_error( "PostgreSQL parameter exceeds the libpq size limit" );

		return static_cast< int >( size );
	}
};

template < typename T >
constexpr int defineParamFormat()
{
	using value_type = strip_optional< T >;
	return std::is_convertible_v< value_type, std::string_view > ? 0 : 1;
}

template < typename T >
std::vector< std::byte > encodeBinaryParameter( const T arg )
{
	return encodeNetwork( arg );
}

inline void appendBinaryInt32( std::vector< std::byte >& output, const std::int32_t value )
{
	auto encoded { encodeBinaryParameter( value ) };
	output.insert( output.end(), encoded.begin(), encoded.end() );
}

template < typename T >
ParamValue createParamValue( T&& arg );

template < typename TVector >
std::vector< std::byte > encodeBinaryArray( TVector&& values )
{
	return std::move( *DBCodec< std::remove_cvref_t< TVector > >::encode( values ).binary );
}

template < typename T >
ParamValue createParamValue( T&& arg )
{
	auto encoded { DBCodec< std::decay_t< T > >::encode( arg ) };
	ParamValue result;
	result.m_is_null = !encoded.text && !encoded.binary;
	result.m_format = static_cast< int >( encoded.format );
	if ( encoded.text ) result.m_text = std::move( *encoded.text );
	if ( encoded.binary ) result.m_binary = std::move( *encoded.binary );
	return result;
}

class IDHANPQContext
{
	PGconn* m_mgmt_conn { nullptr };

	struct Connection
	{
		PGconn* m_conn { nullptr };
	};

  public:

	IDHANPQContext() = delete;
	IDHANPQContext( const IDHANPQContext& ) = delete;
	IDHANPQContext& operator=( const IDHANPQContext& ) = delete;
	IDHANPQContext( std::string_view host, std::uint16_t port, std::string_view db_name );

	template < typename... TArgs >
	DBTask execSql( std::string_view sql, TArgs&&... args )
	{
		auto conn { m_mgmt_conn };

		if constexpr ( sizeof...( args ) == 0 )
		{
			if ( PQsendQuery( conn, sql.data() ) != 1 )
			{
				// failed
			}
			else
			{
				return DBTask { conn };
			}
		}
		else
		{
			constexpr std::size_t parameter_count { sizeof...( args ) };
			static_assert( parameter_count <= std::numeric_limits< int >::max() );
			constexpr int libpq_parameter_count { static_cast< int >( parameter_count ) };
			constexpr static auto param_types { defineParamTypes< strip_optional< TArgs >... >() };

			std::array< ParamValue, parameter_count > parameter_storage {
				{ createParamValue( std::forward< TArgs >( args ) )... }
			};

			std::array< const char*, parameter_count > param_values {};
			std::array< int, parameter_count > param_lengths {};
			std::array< int, parameter_count > param_formats {};

			for ( std::size_t i { 0 }; i < parameter_storage.size(); ++i )
			{
				param_values[ i ] = parameter_storage[ i ].data();
				param_lengths[ i ] = parameter_storage[ i ].size();
				param_formats[ i ] = parameter_storage[ i ].m_format;
			}

			const std::string query { sql };

			if ( PQsendQueryParams(
					 conn,
					 query.c_str(),
					 libpq_parameter_count,
					 param_types.data(),
					 param_values.data(),
					 param_lengths.data(),
					 param_formats.data(),
					 0 )
			     != 1 )
			{
				// failed
			}
			else
			{
				return DBTask { conn };
			}
		}

		return DBTask { nullptr };
	}

	~IDHANPQContext();
};

} // namespace idhan::db

namespace idhan::db
{
template < typename... Ts >
runtime::Task< DBRows< Ts... > > DBTask::rows()
{
	auto raw { co_await *this };
	co_return DBRows< Ts... >( std::move( raw ) );
}
} // namespace idhan::db
