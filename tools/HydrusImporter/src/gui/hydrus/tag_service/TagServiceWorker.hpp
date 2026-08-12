#pragma once

#include "HydrusImporter.hpp"

namespace idhan::hydrus
{
class TransactionBaseCoro;
}

//! QRunnable that imports a single Hydrus tag service into IDHAN — its tag→file mappings and its
//! sibling/parent relationships — reporting progress and errors through the signals below.
class TagServiceWorker final : public QObject, public QRunnable
{
	Q_OBJECT

	idhan::hydrus::ServiceInfo m_service;
	idhan::hydrus::HydrusImporter* m_importer;
	bool m_preprocessed { false };
	idhan::TagDomainID tag_domain_id { 0 };

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

	//! A single (hash, tag) pair read from a Hydrus mappings table.
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
	void errorOccurred( const QString& message );
	void processedMappings( std::size_t count, std::size_t record_count );
	void processedParents( std::size_t count );
	void processedAliases( std::size_t count );
	void processedMaxMappings( std::size_t count );
	void processedMaxParents( std::size_t count );
	void processedMaxAliases( std::size_t count );
};
