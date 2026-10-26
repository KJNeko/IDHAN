#include "idhan/db/DBResult.hpp"

namespace idhan::db
{
void PGResultDeleter::operator()( PGresult* result ) const noexcept
{
	if ( result ) PQclear( result );
}
} // namespace idhan::db
