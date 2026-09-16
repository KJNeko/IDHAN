#pragma once
#include <string>
#include <vector>

#include "RouteSpec.hpp"

namespace idhan::http
{
struct RouteMatch
{
	enum class Status
	{
		found,
		not_found,
		method_not_allowed,
		invalid_parameter
	};
	Status status { Status::not_found };
	std::optional< std::size_t > route_id;
	RequestParameters path_parameters;
	std::vector< proxygen::HTTPMethod > allowed_methods;

	RouteMatch() : status( Status::not_found ), route_id {}, path_parameters {}, allowed_methods {} {}
};

class RouteRegistry
{
	std::vector< RouteDefinition > m_routes;
	bool m_sealed { false };

  public:

	RouteRegistry();
	~RouteRegistry() = default;
	RouteRegistry( RouteRegistry&& ) noexcept = default;
	RouteRegistry& operator=( RouteRegistry&& ) noexcept = default;
	RouteRegistry( const RouteRegistry& ) = delete;
	RouteRegistry& operator=( const RouteRegistry& ) = delete;
	void add( RouteDefinition );
	void seal();
	[[nodiscard]] RouteMatch match( proxygen::HTTPMethod, std::string_view path ) const;
	[[nodiscard]] HTTPTask< Response > dispatch( RouteMatch, RequestContext ) const;
	[[nodiscard]] HTTPTask< std::optional< Response > > validateRequest(
		const RouteMatch&,
		const proxygen::HTTPMessage&,
		const RequestParameters& ) const;
	[[nodiscard]] std::vector< RouteMetadata > routes() const;
};
} // namespace idhan::http
