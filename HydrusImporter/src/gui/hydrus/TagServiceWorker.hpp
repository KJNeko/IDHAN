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

  public:

	bool m_ptr { false };

	TagServiceWorker( QObject* parent, idhan::hydrus::HydrusImporter* importer );

	Q_DISABLE_COPY_MOVE( TagServiceWorker )

	void setService( const idhan::hydrus::ServiceInfo& info );

	void preprocess();
	void importMappings();
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
		const idhan::hydrus::TransactionBaseCoro& mappings_tr, const std::string& current_mappings_name );

  public:

  signals:
	void finished();
	void processedMappings( std::size_t count );
	void processedParents( std::size_t count );
	void processedAliases( std::size_t count );
	void processedMaxMappings( std::size_t count );
	void processedMaxParents( std::size_t count );
	void processedMaxAliases( std::size_t count );
};
