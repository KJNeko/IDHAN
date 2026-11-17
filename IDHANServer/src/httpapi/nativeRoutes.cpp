#include "nativeRoutes.hpp"

#include "apiAuth.hpp"
#include "gen_routes.hpp"

namespace idhan::httpapi
{

void registerNativeRoutes( http::RouteRegistry& registry, db::DBContext& db )
{
	for ( auto& route :
	      http::reflectedRoutes< generated_routes::members >( db, { apiAuthValidation( db ) }, { "idhan_key" } ) )
		registry.add( std::move( route ) );
}

} // namespace idhan::httpapi
