//
// Created by kj16609 on 3/11/25.
//

#include "HydrusImporter.hpp"
#include "sqlitehelper/Transaction.hpp"

namespace idhan::hydrus
{

void HydrusImporter::copySiblings()
{
	logging::info( "Copying siblings" );
	TransactionBase master_tr { master_db };
	TransactionBase client_tr { client_db };

	QNetworkAccessManager m_network {};

	const auto getHydrusTag = [ &master_tr ]( const std::size_t tag_id ) -> std::pair< std::string, std::string >
	{
		std::pair< std::string, std::string > pair {};

		master_tr << "SELECT namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags WHERE tag_id = ?"
				  << tag_id
			>> [ &pair ]( const std::string_view n_tag, const std::string_view s_tag )
		{ pair = std::make_pair( n_tag, s_tag ); };

		return pair;
	};

	// find all services that have parents
	client_tr << "SELECT name, service_id FROM services WHERE service_type = 0 OR service_type = 5" >>
		[ & ]( const std::string name, const std::size_t service_id )
	{
		logging::info( "Getting siblings from service {}", name );

		const std::string table_name { std::format( "current_tag_siblings_{}", service_id ) };

		const QFuture< TagDomainID > domain_id_future { m_client->getTagDomain( name ) };

		const auto domain_id { domain_id_future.result() };

		using TagPair = std::pair< std::string, std::string >;

		std::vector< TagPair > tags {};

		client_tr << std::format( "SELECT bad_tag_id, good_tag_id FROM {}", table_name ) >>
			[ &getHydrusTag, this, domain_id, &tags ]( const std::size_t aliased_id, const std::size_t alias_id )
		{
			const auto aliased_pair { getHydrusTag( aliased_id ) };
			const auto alias_pair { getHydrusTag( alias_id ) };

			tags.emplace_back( aliased_pair );
			tags.emplace_back( alias_pair );
		};

		logging::debug( "Found {} tags to insert", tags.size() );

		QFuture< std::vector< TagID > > tag_ids_future { m_client->createTags( std::move( tags ) ) };

		tag_ids_future.waitForFinished();

		const auto tag_ids { tag_ids_future.result() };

		logging::debug( "Finished creating tags for {} siblings", tag_ids.size() );

		std::vector< std::pair< TagID, TagID > > pairs {};

		for ( std::size_t i = 0; i < tag_ids.size(); i += 2 )
		{
			const auto aliased_id { tag_ids[ i ] };
			const auto alias_id { tag_ids[ i + 1 ] };

			pairs.emplace_back( aliased_id, alias_id );

			if ( pairs.size() > 1024 * 32 )
			{
				m_client->createAliasRelationship( domain_id, std::move( pairs ) ).waitForFinished();
				pairs.clear();
			}
		}

		m_client->createAliasRelationship( domain_id, std::move( pairs ) ).waitForFinished();

		logging::info( "Finished copying siblings for {}", table_name );
	};
}

} // namespace idhan::hydrus