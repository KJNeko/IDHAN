#include "idhan/db/DBError.hpp"

#include <format>
#include <utility>

#include "idhan/db/formatSqlForLog.hpp"

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

static std::string formatDecodeError(
	const std::string& message,
	const std::size_t column,
	const Oid expected,
	const Oid actual,
	const std::string& column_name,
	const std::string& sql,
	const std::optional< std::size_t > row )
{
	auto output { std::format( "{}: column {}", message, column ) };
	if ( !column_name.empty() ) output += std::format( " ({})", column_name );
	output += std::format( " (zero-based), expected OID {}, actual OID {}", expected, actual );
	if ( row ) output += std::format( "; row {} (zero-based)", *row );
	if ( !sql.empty() ) output += std::format( "; SQL: {}", formatSqlForLog( sql ) );
	return output;
}

DBDecodeError::DBDecodeError(
	const std::string& message,
	const std::size_t column,
	const Oid expected,
	const Oid actual,
	const std::string& column_name,
	const std::string& sql,
	const std::optional< std::size_t > row ) :
  std::runtime_error( formatDecodeError( message, column, expected, actual, column_name, sql, row ) ),
  message( message ),
  column( column ),
  expected_oid( expected ),
  actual_oid( actual )
{}
} // namespace idhan::db
