#include "idhan/db/DBError.hpp"

#include <utility>

namespace idhan::db
{
DBQueryError::DBQueryError( DBDiagnostics diagnostics ) :
  std::runtime_error( diagnostics.message ),
  m_diagnostics( std::move( diagnostics ) )
{}

const DBDiagnostics& DBQueryError::diagnostics() const noexcept
{
	return m_diagnostics;
}

DBConstraintError::DBConstraintError( DBDiagnostics diagnostics ) : DBQueryError( std::move( diagnostics ) )
{}

DBQueryTimeout::DBQueryTimeout( DBDiagnostics diagnostics ) : DBQueryError( std::move( diagnostics ) )
{}

DBOperationCancelled::DBOperationCancelled() : std::runtime_error( "Database operation cancelled" )
{}

DBDecodeError::DBDecodeError( std::string message, std::size_t column, Oid expected, Oid actual ) :
  std::runtime_error( std::move( message ) ),
  column( column ),
  expected_oid( expected ),
  actual_oid( actual )
{}
} // namespace idhan::db
