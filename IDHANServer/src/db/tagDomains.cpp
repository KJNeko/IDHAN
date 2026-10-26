#include "tagDomains.hpp"

#include <optional>
#include <stdexcept>
#include <utility>

#include "IDHANDB.hpp"
#include "idhan/db/DBTransaction.hpp"

namespace idhan::db
{

static runtime::Task< std::optional< TagDomainID > > findTagDomainId(
	DBTransaction& transaction,
	const std::string& name )
{
	DBBindings lookup_bindings {};
	lookup_bindings.bind( name );
	const auto existing {
		co_await transaction
			.query( "SELECT tag_domain_id FROM tag_domains WHERE domain_name = $1", std::move( lookup_bindings ) )
			.rows< TagDomainID >()
	};
	if ( existing.empty() ) co_return std::nullopt;

	co_return std::get< 0 >( existing.row( 0 ) );
}

runtime::Task< TagDomainID > getOrCreateTagDomain( DBTransaction& transaction, std::string name )
{
	const auto existing { co_await findTagDomainId( transaction, name ) };
	if ( existing.has_value() ) co_return existing.value();

	// A BEFORE INSERT trigger creates partitions, even for an insert that later conflicts.
	// Lock and recheck in a fresh statement so concurrent imports cannot leave orphan partitions.
	co_await transaction.query( "LOCK TABLE tag_domains IN SHARE ROW EXCLUSIVE MODE" ).result();
	const auto concurrent { co_await findTagDomainId( transaction, name ) };
	if ( concurrent.has_value() ) co_return concurrent.value();

	DBBindings create_bindings {};
	create_bindings.bind( std::move( name ) );
	const auto created {
		co_await transaction
			.query(
				"INSERT INTO tag_domains (domain_name) VALUES ($1) RETURNING tag_domain_id",
				std::move( create_bindings ) )
			.rows< TagDomainID >()
	};
	if ( created.empty() ) throw std::runtime_error( "Unable to create tag domain" );

	co_return std::get< 0 >( created.row( 0 ) );
}

} // namespace idhan::db
