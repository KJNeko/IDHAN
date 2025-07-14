//
// Created by kj16609 on 3/11/25.
//

#include "HydrusImporter.hpp"
#include "fgl/ProgressBar.hpp"
#include "hydrus_constants.hpp"
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
	client_tr << "SELECT name, service_id, service_type FROM services WHERE service_type = 0 OR service_type = 5" >>
		[ & ]( const std::string name, const std::size_t service_id, const std::size_t service_type )
	{
		if ( !m_process_ptr_mappings && service_type == hy_constants::ServiceTypes::PTR_SERVICE )
		{
			// if the current table is for the ptr, and we are not told to process the ptr mappings, then skip this
			return;
		}

		logging::info( "Getting siblings from service {}", name );

		const std::string table_name { std::format( "current_tag_siblings_{}", service_id ) };

		const QFuture< TagDomainID > domain_id_future { IDHANClient::instance().getTagDomain( name ) };

		const auto domain_id { domain_id_future.result() };

		using TagPair = std::pair< std::string, std::string >;

		std::vector< TagPair > tags {};

		std::size_t max_count { 0 };
		client_tr << std::format( "SELECT count(*) FROM {}", table_name ) >> max_count;

		auto parents_progress { fgl::ProgressBar::getInstance().addProgressBar( table_name ) };
		parents_progress->setMax( max_count );

		client_tr << std::format( "SELECT bad_tag_id, good_tag_id FROM {}", table_name ) >>
			[ &getHydrusTag, &tags, &parents_progress ]( const std::size_t aliased_id, const std::size_t alias_id )
		{
			const auto aliased_pair { getHydrusTag( aliased_id ) };
			const auto alias_pair { getHydrusTag( alias_id ) };

			tags.emplace_back( aliased_pair );
			tags.emplace_back( alias_pair );

			parents_progress->inc();
		};

		QFuture< std::vector< TagID > > tag_ids_future { IDHANClient::instance().createTags( std::move( tags ) ) };

		tag_ids_future.waitForFinished();

		const auto tag_ids { tag_ids_future.result() };

		std::vector< std::pair< TagID, TagID > > pairs {};

		for ( std::size_t i = 0; i < tag_ids.size(); i += 2 )
		{
			const auto aliased_id { tag_ids[ i ] };
			const auto alias_id { tag_ids[ i + 1 ] };

			pairs.emplace_back( aliased_id, alias_id );

			if ( pairs.size() > 1024 * 32 )
			{
				IDHANClient::instance().createAliasRelationship( domain_id, std::move( pairs ) ).waitForFinished();
				pairs.clear();
			}
		}

		IDHANClient::instance().createAliasRelationship( domain_id, std::move( pairs ) ).waitForFinished();

		logging::info( "Finished copying siblings for {}", table_name );
	};
}

} // namespace idhan::hydrus