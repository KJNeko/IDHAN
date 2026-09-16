#pragma once
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <proxygen/lib/http/coro/HTTPCoroSession.h>
#include <proxygen/lib/http/coro/server/HTTPServer.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>

#include "HTTPConfig.hpp"
#include "RouteRegistry.hpp"

namespace idhan
{
class IDHANServerContext;
}

namespace idhan::http
{
class HTTPServer final : public proxygen::coro::HTTPHandler
{
	IDHANServerContext* m_context {};
	HTTPConfig m_config;
	RouteRegistry m_routes;
	runtime::DrainRegistry m_drain;
	folly::CPUThreadPoolExecutor m_body_executor { 2 };
	std::optional< proxygen::coro::HTTPServer > m_server;
	std::thread m_thread;
	std::atomic< RequestID > m_next_request_id { 1 };
	std::uint16_t m_port {};
	bool m_started { false };

  public:

	HTTPServer( HTTPConfig, RouteRegistry );
	HTTPServer( IDHANServerContext&, HTTPConfig, RouteRegistry );
	~HTTPServer() override;
	HTTPServer( const HTTPServer& ) = delete;
	HTTPServer& operator=( const HTTPServer& ) = delete;
	void start();
	void stopAdmission();
	[[nodiscard]] HTTPTask<> drain();
	void stop();
	[[nodiscard]] std::uint16_t port() const noexcept;
	[[nodiscard]] HTTPTask< proxygen::coro::HTTPSourceHolder > handleRequest(
		folly::EventBase*,
		proxygen::coro::HTTPSessionContextPtr,
		proxygen::coro::HTTPSourceHolder ) override;
};
} // namespace idhan::http
