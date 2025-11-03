//
// Created by kj16609 on 6/29/25.
//
#pragma once

#include "HydrusImporter.hpp"

namespace idhan::hydrus
{
class TransactionBaseCoro;
}

class TagServiceWorker final : public QObject, public QRunnable
{
	Q_OBJECT

	idhan::hydrus::ServiceInfo m_service;
	idhan::hydrus::HydrusImporter* m_importer;
	bool m_preprocessed { false };
	bool m_processing { false };
	idhan::TagDomainID tag_domain_id { 0 };

	mutable std::queue< QFuture< void > > m_futures {};
	constexpr static auto sem_count { 4 };
	mutable std::counting_semaphore< sem_count > mappings_semaphore { sem_count };

  public:

	bool m_ptr { false };

	TagServiceWorker( QObject* parent, idhan::hydrus::HydrusImporter* importer );

	Q_DISABLE_COPY_MOVE( TagServiceWorker )

	void setService( const idhan::hydrus::ServiceInfo& info );

	void preprocess();
	void importMappings();
	void processSiblings(
		const std::vector< std::pair< int, int > >& hy_siblings,
		const std::unordered_map< int, std::pair< std::string, std::string > >& tag_pairs,
		const std::unordered_map< int, idhan::TagID >& tag_translation_map,
		size_t set_limit );
	void processParents(
		const std::vector< std::pair< int, int > >& hy_parents,
		const std::unordered_map< int, idhan::TagID >& tag_translation_map,
		size_t set_limit );
	void processRelationships();
	void run() override;

  private:

	struct MappingPair
	{
		int hash_id;
		int tag_id;
	};

	void processPairs( const std::vector< MappingPair >& pairs ) const;
	void processParents( const std::vector< std::pair< idhan::TagID, idhan::TagID > >& pairs ) const;
	void processSiblings( const std::vector< std::pair< idhan::TagID, idhan::TagID > >& pairs ) const;

	void processMappingsBatch(
		const idhan::hydrus::TransactionBaseCoro& mappings_tr,
		const std::string& current_mappings_name );

  public:

  signals:
	void finished();
	void processedMappings( std::size_t count, std::size_t record_count );
	void processedParents( std::size_t count );
	void processedAliases( std::size_t count );
	void processedMaxMappings( std::size_t count );
	void processedMaxParents( std::size_t count );
	void processedMaxAliases( std::size_t count );
};
