#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include <pqxx/pqxx>
#pragma GCC diagnostic pop

#include "ApiClient.hpp"
#include "ServerProcess.hpp"

namespace idhan::test
{

//! One server, one migrated schema, one API key, shared by every test in the binary.
class TestServer
{
	pqxx::connection m_connection;
	std::string m_schema;
	ServerProcess m_server;
	ApiClient m_client;

  public:

	TestServer();

	TestServer( const TestServer& ) = delete;
	TestServer& operator=( const TestServer& ) = delete;

	~TestServer();

	ApiClient& client() { return m_client; }

	pqxx::connection& connection() { return m_connection; }

	//! Returns the schema to the state a freshly migrated one is in: no tags, no domains beyond the default.
	void wipe();
};

[[nodiscard]] TestServer& testServer();

//! Wipes the tag tables when the test that used them ends.
class ServerFixture
{
  protected:

	ApiClient& api() const { return testServer().client(); }

	pqxx::connection& db() const { return testServer().connection(); }

	//! True when the endpoints are answering without an API key because auth was compiled out.
	[[nodiscard]] bool authDisabled() const;

  public:

	ServerFixture() = default;

	ServerFixture( const ServerFixture& ) = delete;
	ServerFixture& operator=( const ServerFixture& ) = delete;

	~ServerFixture();
};

//! True when a partition of that name exists for the domain, eg tag_mappings_domain_3.
[[nodiscard]] bool partitionExists( pqxx::connection& connection, std::string_view prefix, TagDomainID tag_domain_id );

} // namespace idhan::test
