#include <concepts>

#include "idhan/db/DBConnection.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBError.hpp"
#include "idhan/db/DBOperation.hpp"
#include "idhan/db/DBReactor.hpp"
#include "idhan/db/PreparedStatementCache.hpp"

using namespace idhan;
static_assert( !std::copy_constructible< db::DBTask > );
static_assert( std::move_constructible< db::DBTransaction > );
static_assert(
	std::same_as< decltype( std::declval< db::DBTask& >().rows< int >() ), runtime::Task< db::DBRows< int > > > );
static_assert( std::same_as<
			   decltype( std::declval< db::DBTask& >().nextRow< int >() ),
			   runtime::Task< std::optional< std::tuple< int > > > > );
