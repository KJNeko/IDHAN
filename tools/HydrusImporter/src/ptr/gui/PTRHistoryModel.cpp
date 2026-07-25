#include "PTRHistoryModel.hpp"

#include <QLocale>

using idhan::hydrus::ptr::PTRHistoryEntry;

PTRHistoryModel::PTRHistoryModel( QObject* parent ) : QAbstractTableModel( parent )
{}

int PTRHistoryModel::rowCount( const QModelIndex& parent ) const
{
	if ( parent.isValid() ) return 0;
	return static_cast< int >( m_entries.size() );
}

int PTRHistoryModel::columnCount( const QModelIndex& parent ) const
{
	if ( parent.isValid() ) return 0;
	return ColumnCount;
}

QVariant PTRHistoryModel::data( const QModelIndex& index, int role ) const
{
	if ( !index.isValid() || role != Qt::DisplayRole ) return {};

	const auto row = static_cast< std::size_t >( index.row() );
	if ( row >= m_entries.size() ) return {};

	const auto& entry = m_entries[ row ];
	const auto& stats = entry.stats;

	switch ( index.column() )
	{
		case UpdateIndex:
			return QLocale::system().toString( entry.update_index );
		case FileCount:
			return QLocale::system().toString( static_cast< qlonglong >( entry.file_count ) );
		case RecordsCreated:
			return QLocale::system().toString( stats.records_created );
		case TagsCreated:
			return QLocale::system().toString( stats.tags_created );
		case MappingsAdded:
			return QLocale::system().toString( stats.mappings_added );
		case MappingsRemoved:
			return QLocale::system().toString( stats.mappings_removed );
		case ParentsAdded:
			return QLocale::system().toString( stats.parents_added );
		case ParentsRemoved:
			return QLocale::system().toString( stats.parents_removed );
		case AliasesAdded:
			return QLocale::system().toString( stats.aliases_added );
		case AliasesRemoved:
			return QLocale::system().toString( stats.aliases_removed );
		default:
			return {};
	}
}

QVariant PTRHistoryModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
	if ( role != Qt::DisplayRole ) return QAbstractTableModel::headerData( section, orientation, role );

	if ( orientation == Qt::Vertical ) return {};

	switch ( section )
	{
		case UpdateIndex:
			return QStringLiteral( "Update #" );
		case FileCount:
			return QStringLiteral( "Files" );
		case RecordsCreated:
			return QStringLiteral( "Records" );
		case TagsCreated:
			return QStringLiteral( "Tags" );
		case MappingsAdded:
			return QStringLiteral( "Mappings +" );
		case MappingsRemoved:
			return QStringLiteral( "Mappings −" );
		case ParentsAdded:
			return QStringLiteral( "Parents +" );
		case ParentsRemoved:
			return QStringLiteral( "Parents −" );
		case AliasesAdded:
			return QStringLiteral( "Aliases +" );
		case AliasesRemoved:
			return QStringLiteral( "Aliases −" );
		default:
			return {};
	}
}

void PTRHistoryModel::addEntry( const PTRHistoryEntry& entry )
{
	const int row = static_cast< int >( m_entries.size() );
	beginInsertRows( QModelIndex(), row, row );
	m_entries.push_back( entry );
	endInsertRows();
}

void PTRHistoryModel::clear()
{
	if ( m_entries.empty() ) return;
	beginResetModel();
	m_entries.clear();
	endResetModel();
}
