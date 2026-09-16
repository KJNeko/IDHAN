#pragma once

#include <cstdint>

#include "idhan/http/HTTPServer.hpp"

namespace idhan::http::test
{
class EchoServer final
{
	folly::CPUThreadPoolExecutor m_file_executor { 1 };
	HTTPServer m_server;

  public:

	EchoServer();
	void start();
	void stop();
	[[nodiscard]] std::uint16_t port() const noexcept;
};
} // namespace idhan::http::test
