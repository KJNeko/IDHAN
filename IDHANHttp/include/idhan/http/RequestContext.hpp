#pragma once
#include <folly/io/IOBuf.h>
#include <proxygen/lib/http/HTTPMessage.h>

#include <string>
#include <variant>

#include "HTTPConfig.hpp"
#include "RequestParameters.hpp"
#include "TemporaryUpload.hpp"
#include "idhan/runtime/DrainRegistry.hpp"

namespace idhan
{
class IDHANServerContext;
}

namespace idhan::http
{
struct ParameterRules;
// An empty body is represented by an empty IOBuf, never a null pointer.
using RequestBody = std::variant< std::unique_ptr< folly::IOBuf >, TemporaryUpload >;

class RequestContext
{
	IDHANServerContext* m_server {};
	RequestID m_id;
	proxygen::HTTPMessage m_message;
	RequestParameters m_parameters;
	RequestBody m_body;
	runtime::WorkRegistration m_work;
	bool m_transport_open { true };
	const ParameterRules* m_parameter_rules {};

  public:

	RequestContext(
		IDHANServerContext&,
		RequestID,
		proxygen::HTTPMessage,
		RequestParameters,
		RequestBody,
		runtime::WorkRegistration );
	// A null server context is valid only for transport-level library tests.
	RequestContext(
		IDHANServerContext*,
		RequestID,
		proxygen::HTTPMessage,
		RequestParameters,
		RequestBody,
		runtime::WorkRegistration );
	RequestContext( RequestID, proxygen::HTTPMessage, RequestParameters, RequestBody, runtime::WorkRegistration );
	~RequestContext() = default;
	RequestContext( RequestContext&& ) noexcept = default;
	RequestContext& operator=( RequestContext&& ) noexcept = delete;
	RequestContext( const RequestContext& ) = delete;
	RequestContext& operator=( const RequestContext& ) = delete;
	[[nodiscard]] bool hasServerContext() const noexcept;
	[[nodiscard]] IDHANServerContext& server() const;
	[[nodiscard]] RequestID id() const noexcept;
	[[nodiscard]] const proxygen::HTTPMessage& message() const noexcept;
	[[nodiscard]] const RequestParameters& parameters() const noexcept;
	// Rules are borrowed from the sealed route registry, which outlives the request.
	void setParameterRules( const ParameterRules* rules ) noexcept;
	[[nodiscard]] const ParameterRules* parameterRules() const noexcept;
	[[nodiscard]] const RequestBody& body() const noexcept;
	void setBody( RequestBody body );
	[[nodiscard]] RequestBody takeBody() &&;
	[[nodiscard]] runtime::WorkRegistration childWork() const;
	[[nodiscard]] bool transportOpen() const noexcept;
};
} // namespace idhan::http
