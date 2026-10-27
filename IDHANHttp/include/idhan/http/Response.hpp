#pragma once
#include <folly/io/IOBuf.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/coro/HTTPSourceHolder.h>

#include <memory>
#include <optional>
#include <variant>

#include "idhan/runtime/Task.hpp"

namespace idhan::http
{

template < typename T = void >
using HTTPTask = runtime::Task< T >;

class Response
{
  public:

	using Body = std::variant< std::monostate, std::unique_ptr< folly::IOBuf >, proxygen::coro::HTTPSourceHolder >;

	proxygen::HTTPMessage message {};
	Body body {};
	std::optional< proxygen::HTTPHeaders > trailers {};

	Response();
	~Response();

	Response( Response&& ) noexcept;
	Response& operator=( Response&& ) noexcept;
	Response( const Response& ) = delete;
	Response& operator=( const Response& ) = delete;

	[[nodiscard]] static Response error( std::uint16_t status, std::string message );
	[[nodiscard]] static Response bytes(
		std::uint16_t status,
		std::unique_ptr< folly::IOBuf >,
		std::string content_type );
	[[nodiscard]] static Response json( std::uint16_t status, std::unique_ptr< folly::IOBuf > );
};

} // namespace idhan::http
