#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace idhan::test
{

struct TestRequest
{
	std::string method {};
	std::string target {};
	std::string path {};
	std::map< std::string, std::string > headers {};
	std::string body {};
};

struct TestResponse
{
	int status { 200 };
	std::string reason { "OK" };
	std::vector< std::pair< std::string, std::string > > headers {};
	std::string body {};
};

class TestServer
{
  public:

	using Handler = std::function< TestResponse( const TestRequest& ) >;

  private:

	int m_listen { -1 };
	std::uint16_t m_port {};
	std::atomic_bool m_running { true };
	mutable std::mutex m_mutex {};
	std::map< std::string, Handler > m_routes {};
	std::vector< TestRequest > m_seen {};
	std::atomic_size_t m_open_connections {};
	std::atomic_size_t m_accepted_connections {};
	std::vector< std::jthread > m_workers {};
	std::jthread m_acceptor {};

	void accept();
	void serve( int socket );

  public:

	TestServer();
	TestServer( const TestServer& ) = delete;
	TestServer& operator=( const TestServer& ) = delete;
	~TestServer();

	[[nodiscard]] std::uint16_t port() const { return m_port; }

	[[nodiscard]] std::string url( std::string_view path ) const;

	void route( std::string path, Handler handler );

	[[nodiscard]] std::vector< TestRequest > requests() const;
	[[nodiscard]] std::size_t requestCount() const;

	[[nodiscard]] std::size_t acceptedConnections() const { return m_accepted_connections.load(); }

	[[nodiscard]] std::size_t openConnections() const { return m_open_connections.load(); }

	void stop();
};

} // namespace idhan::test
