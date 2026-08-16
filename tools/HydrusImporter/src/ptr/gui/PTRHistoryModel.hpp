#pragma once

#include <QAbstractTableModel>

#include <vector>

#include "ptr/PTRImportWorker.hpp"

//! Table model backing the PTR import "History" view, one row per completed PTR update batch.
class PTRHistoryModel final : public QAbstractTableModel
{
	Q_OBJECT

  public:

	enum Column
	{
		UpdateIndex = 0,
		FileCount,
		RecordsCreated,
		TagsCreated,
		MappingsAdded,
		MappingsRemoved,
		ParentsAdded,
		ParentsRemoved,
		AliasesAdded,
		AliasesRemoved,
		ColumnCount
	};

	explicit PTRHistoryModel( QObject* parent = nullptr );

	int rowCount( const QModelIndex& parent = QModelIndex() ) const override;
	int columnCount( const QModelIndex& parent = QModelIndex() ) const override;
	QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const override;
	QVariant headerData( int section, Qt::Orientation orientation, int role = Qt::DisplayRole ) const override;

	void addEntry( const idhan::hydrus::ptr::PTRHistoryEntry& entry );
	void clear();

  private:

	std::vector< idhan::hydrus::ptr::PTRHistoryEntry > m_entries;
};
