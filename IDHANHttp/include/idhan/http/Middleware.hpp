#pragma once
#include <functional>
#include <memory>
#include <vector>

#include "RouteSpec.hpp"

namespace idhan::http
{
using NextHandler = std::function< HTTPTask< Response >( RequestContext ) >;

class Middleware
{
  public:

	virtual ~Middleware();
	[[nodiscard]] virtual HTTPTask< Response > invoke( RequestContext, NextHandler ) const = 0;
};

class MiddlewareChain
{
	std::vector< std::shared_ptr< const Middleware > > m_chain;

  public:

	explicit MiddlewareChain( std::vector< std::shared_ptr< const Middleware > > );
	[[nodiscard]] HTTPTask< Response > invoke( RequestContext, NextHandler ) const;
};
} // namespace idhan::http
