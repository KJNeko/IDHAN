#include "TagManagementWidget.hpp"

#include <QFutureWatcher>

#include <idhan/IDHANClient.hpp>

#include "TagDomainRelationshipWidget.hpp"
#include "ui_TagManagementWidget.h"

TagManagementWidget::TagManagementWidget( QWidget* parent ) :
  QWidget( parent ),
  ui( new Ui::TagManagementWidget ),
  m_autocompleteWatcher( new QFutureWatcher< std::vector< std::pair< idhan::TagID, std::string > > >( this ) )
{
	ui->setupUi( this );
	connect(
		m_autocompleteWatcher,
		&QFutureWatcher< std::vector< std::pair< idhan::TagID, std::string > > >::finished,
		this,
		&TagManagementWidget::on_autocompleteFinished );
	loadDomains();
}

TagManagementWidget::~TagManagementWidget()
{
	delete ui;
}

void TagManagementWidget::loadDomains()
{
	auto future = idhan::IDHANClient::instance().getTagDomains();
	auto watcher = new QFutureWatcher< std::vector< idhan::TagDomainInfo > >( this );
	connect(
		watcher,
		&QFutureWatcher< std::vector< idhan::TagDomainInfo > >::finished,
		this,
		[ this, watcher ]()
		{
			m_domains = watcher->result();
			ui->domainTabs->clear();
			m_domainWidgets.clear();
			for ( const auto& domain : m_domains )
			{
				auto domainWidget = new TagDomainRelationshipWidget( domain.m_id, this );
				ui->domainTabs->addTab(
					domainWidget,
					QString( "%1 (%2)" ).arg( QString::fromStdString( domain.m_name ) ).arg( domain.m_id ) );
				m_domainWidgets.push_back( domainWidget );
			}
			watcher->deleteLater();
		} );
	watcher->setFuture( future );
}

void TagManagementWidget::on_tagSearchEdit_textChanged( const QString& text )
{
	if ( text.length() < 2 )
	{
		ui->autocompleteList->clear();
		return;
	}

	if ( m_autocompleteWatcher->isRunning() )
	{
		m_autocompleteWatcher->cancel();
	}

	auto future = idhan::IDHANClient::instance().autocompleteTag( text );
	m_autocompleteWatcher->setFuture( future );
}

void TagManagementWidget::on_autocompleteFinished()
{
	ui->autocompleteList->clear();
	const auto results = m_autocompleteWatcher->result();
	if ( results.empty() )
	{
		auto item = new QListWidgetItem( "No tags found", ui->autocompleteList );
		item->setFlags( item->flags() & ~Qt::ItemIsSelectable );
		item->setForeground( Qt::gray );
	}
	else
	{
		for ( const auto& [ id, tagText ] : results )
		{
			auto item = new QListWidgetItem( QString::fromStdString( tagText ), ui->autocompleteList );
			item->setData( Qt::UserRole, QVariant::fromValue( id ) );
		}
	}
}

void TagManagementWidget::on_tagSearchEdit_returnPressed()
{
	if ( ui->autocompleteList->count() > 0 )
	{
		auto* item = ui->autocompleteList->item( 0 );
		if ( item->flags() & Qt::ItemIsSelectable )
		{
			on_autocompleteList_itemClicked( item );
		}
	}
}

void TagManagementWidget::on_autocompleteList_itemClicked( QListWidgetItem* item )
{
	auto idVariant = item->data( Qt::UserRole );
	if ( !idVariant.isValid() ) return;

	m_selectedTagID = idVariant.value< idhan::TagID >();
	ui->currentTagEdit->setText( item->text() );

	for ( auto* widget : m_domainWidgets )
	{
		widget->setTag( m_selectedTagID );
	}
}
