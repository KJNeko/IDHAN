#include "TagManagementWidget.hpp"

#include <QFutureWatcher>
#include <QLabel>
#include <QShowEvent>

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
}

TagManagementWidget::~TagManagementWidget()
{
	delete ui;
}

void TagManagementWidget::showEvent( QShowEvent* event )
{
	QWidget::showEvent( event );
	if ( !m_domainsLoaded ) loadDomains();
}

void TagManagementWidget::loadDomains()
{
	if ( m_domainWatcher != nullptr ) return;

	m_domainWatcher = new QFutureWatcher< std::vector< idhan::TagDomainInfo > >( this );
	connect(
		m_domainWatcher,
		&QFutureWatcher< std::vector< idhan::TagDomainInfo > >::finished,
		this,
		[ this ]()
		{
			auto* watcher = m_domainWatcher;
			m_domainWatcher = nullptr;
			watcher->deleteLater();

			std::vector< idhan::TagDomainInfo > domains {};

			try
			{
				domains = watcher->result();
			}
			catch ( std::exception& e )
			{
				idhan::logging::error( "Failed to load tag domains: {}", e.what() );
				showDomainLoadFailure();
				return;
			}
			catch ( ... )
			{
				idhan::logging::error( "Failed to load tag domains" );
				showDomainLoadFailure();
				return;
			}

			m_domains = std::move( domains );
			m_domainsLoaded = true;

			clearDomainTabs();
			for ( const auto& domain : m_domains )
			{
				auto domainWidget = new TagDomainRelationshipWidget( domain.m_id, this );
				ui->domainTabs->addTab(
					domainWidget,
					QString( "%1 (%2)" ).arg( QString::fromStdString( domain.m_name ) ).arg( domain.m_id ) );
				m_domainWidgets.push_back( domainWidget );
			}

			if ( m_selectedTagID != 0 )
			{
				for ( auto* widget : m_domainWidgets ) widget->setTag( m_selectedTagID );
			}
		} );

	m_domainWatcher->setFuture( idhan::IDHANClient::instance().getTagDomains() );
}

void TagManagementWidget::clearDomainTabs()
{
	while ( ui->domainTabs->count() > 0 )
	{
		auto* page = ui->domainTabs->widget( 0 );
		ui->domainTabs->removeTab( 0 );
		page->deleteLater();
	}

	m_domainWidgets.clear();
}

void TagManagementWidget::showDomainLoadFailure()
{
	clearDomainTabs();

	auto* label = new QLabel( "Could not reach the server. Switch away and back to this tab to retry.", this );
	label->setAlignment( Qt::AlignCenter );
	label->setWordWrap( true );
	ui->domainTabs->addTab( label, "Unavailable" );
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
	if ( m_autocompleteWatcher->isCanceled() ) return;

	std::vector< std::pair< idhan::TagID, std::string > > results {};

	try
	{
		results = m_autocompleteWatcher->result();
	}
	catch ( std::exception& e )
	{
		idhan::logging::error( "Tag autocomplete failed: {}", e.what() );
		return;
	}
	catch ( ... )
	{
		idhan::logging::error( "Tag autocomplete failed" );
		return;
	}

	ui->autocompleteList->clear();
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
