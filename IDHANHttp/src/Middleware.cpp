#include "idhan/http/Middleware.hpp"

namespace idhan::http
{

Middleware::~Middleware() = default;

MiddlewareChain::MiddlewareChain( std::vector< std::shared_ptr< const Middleware > > chain ) :
  m_chain( std::move( chain ) )
{}

HTTPTask< Response > MiddlewareChain::invoke( RequestContext context, NextHandler next ) const
{
	for ( auto iterator { m_chain.rbegin() }; iterator != m_chain.rend(); ++iterator )
	{
		const auto middleware { *iterator };
		const auto following { std::move( next ) };
		next = [ middleware, following ]( RequestContext request ) -> HTTPTask< Response >
		{ co_return co_await middleware->invoke( std::move( request ), following ); };
	}

	co_return co_await next( std::move( context ) );
}

} // namespace idhan::http
