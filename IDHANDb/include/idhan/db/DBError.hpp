#pragma once
#include <cstddef>
#include <cstdint>
#include <libpq-fe.h>
#include <optional>
#include <stdexcept>
#include <string>

namespace idhan::db
{

struct DBDiagnostics
{
	std::string sqlstate, severity, message, detail, hint, context;
	std::string schema, table, column, data_type, constraint;
	std::uint64_t query_id { 0 };
};

class DBQueryError : public std::runtime_error
{
	DBDiagnostics m_diagnostics;

  public:

	explicit DBQueryError( DBDiagnostics );
	[[nodiscard]] const DBDiagnostics& diagnostics() const noexcept;
};

class DBConstraintError final : public DBQueryError
{
  public:

	explicit DBConstraintError( DBDiagnostics );
};

class DBQueryTimeout final : public DBQueryError
{
  public:

	explicit DBQueryTimeout( DBDiagnostics );
};

class DBDecodeError final : public std::runtime_error
{
  public:

	const std::string message;
	const std::size_t column;
	const Oid expected_oid;
	const Oid actual_oid;
	DBDecodeError(
		const std::string& message,
		std::size_t column,
		Oid expected,
		Oid actual,
		const std::string& column_name = {},
		const std::string& sql = {},
		std::optional< std::size_t > row = {} );
};

class DBOperationCancelled final : public std::runtime_error
{
  public:

	DBOperationCancelled();
};

[[noreturn]] void fatalConnectionFailure( std::uint64_t connection_id, std::string operation, DBDiagnostics );

} // namespace idhan::db
