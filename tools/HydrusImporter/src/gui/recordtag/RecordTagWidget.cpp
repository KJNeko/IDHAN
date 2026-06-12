#include "RecordTagWidget.hpp"

#include <QHBoxLayout>
#include <QNetworkReply>
#include <QNetworkRequest>

RecordTagWidget::RecordTagWidget( QWidget* parent ) :
  QWidget( parent ),
  m_networkManager( new QNetworkAccessManager( this ) )
{
	auto* main_layout = new QVBoxLayout( this );

	// Top row: Record ID input, Load button, Domain selector, Detach
	auto* top_layout = new QHBoxLayout();
	auto* record_id_label = new QLabel( "Record ID:", this );
	m_recordIdInput = new QLineEdit( this );
	m_recordIdInput->setPlaceholderText( "Enter record ID" );
	m_loadButton = new QPushButton( "Load", this );
	m_domainCombo = new QComboBox( this );
	m_detachButton = new QPushButton( "Detach", this );

	top_layout->addWidget( record_id_label );
	top_layout->addWidget( m_recordIdInput );
	top_layout->addWidget( m_loadButton );
	top_layout->addWidget( m_domainCombo );
	top_layout->addWidget( m_detachButton );
	main_layout->addLayout( top_layout );

	// Middle: Preview + Raw Tags + Active Tags
	auto* middle_layout = new QHBoxLayout();

	m_previewLabel = new QLabel( this );
	m_previewLabel->setFixedSize( 256, 256 );
	m_previewLabel->setAlignment( Qt::AlignCenter );
	m_previewLabel->setStyleSheet( "border: 1px solid gray;" );
	m_previewLabel->setText( "No preview" );
	middle_layout->addWidget( m_previewLabel );

	auto* raw_group = new QWidget( this );
	auto* raw_layout = new QVBoxLayout( raw_group );
	auto* raw_label = new QLabel( "Raw Tags (tag_mappings)", raw_group );
	m_rawTagsList = new QListWidget( raw_group );
	raw_layout->addWidget( raw_label );
	raw_layout->addWidget( m_rawTagsList );
	middle_layout->addWidget( raw_group );

	auto* active_group = new QWidget( this );
	auto* active_layout = new QVBoxLayout( active_group );
	auto* active_label = new QLabel( "Active Tags (active_tag_mappings_final)", active_group );
	m_activeTagsList = new QListWidget( active_group );
	active_layout->addWidget( active_label );
	active_layout->addWidget( m_activeTagsList );
	middle_layout->addWidget( active_group );

	main_layout->addLayout( middle_layout );

	// Bottom: Add/Remove controls
	auto* bottom_layout = new QHBoxLayout();
	m_addTagInput = new QLineEdit( this );
	m_addTagInput->setPlaceholderText( "Type to search tags..." );
	m_addTagButton = new QPushButton( "Add Tag", this );
	m_removeTagButton = new QPushButton( "Remove Selected", this );

	bottom_layout->addWidget( m_addTagInput );
	bottom_layout->addWidget( m_addTagButton );
	bottom_layout->addWidget( m_removeTagButton );
	main_layout->addLayout( bottom_layout );

	// Autocomplete popup
	m_autocompletePopup = new QListWidget( this );
	m_autocompletePopup->setWindowFlags( Qt::Popup | Qt::FramelessWindowHint );
	m_autocompletePopup->setFocusPolicy( Qt::NoFocus );
	m_autocompletePopup->setMouseTracking( true );
	m_autocompletePopup->hide();

	// Connections
	connect( m_loadButton, &QPushButton::clicked, this, &RecordTagWidget::onLoadRecord );
	connect( m_recordIdInput, &QLineEdit::returnPressed, this, &RecordTagWidget::onLoadRecord );
	connect( m_detachButton, &QPushButton::clicked, this, &RecordTagWidget::onDetach );
	connect( m_addTagButton, &QPushButton::clicked, this, &RecordTagWidget::onAddTag );
	connect( m_addTagInput, &QLineEdit::returnPressed, this, &RecordTagWidget::onAddTagReturnPressed );
	connect( m_removeTagButton, &QPushButton::clicked, this, &RecordTagWidget::onRemoveTag );
	connect( m_addTagInput, &QLineEdit::textChanged, this, &RecordTagWidget::onAddTagTextChanged );
	connect( m_autocompletePopup, &QListWidget::itemClicked, this, &RecordTagWidget::onAutocompleteItemClicked );

	loadDomains();
}

RecordTagWidget::~RecordTagWidget() = default;

void RecordTagWidget::loadDomains()
{
	auto& client { idhan::IDHANClient::instance() };
	auto future { client.getTagDomains() };

	m_domainWatcher = new QFutureWatcher< std::vector< idhan::TagDomainInfo > >( this );
	m_domainWatcher->setFuture( future );

	connect(
		m_domainWatcher,
		&QFutureWatcherBase::finished,
		this,
		[ this ]()
		{
			try
			{
				m_domains = m_domainWatcher->result();
				m_domainCombo->clear();
				for ( const auto& domain : m_domains )
				{
					m_domainCombo->addItem(
						QString::fromStdString( format_ns::format( "{} ({})", domain.m_name, domain.m_id ) ),
						static_cast< qlonglong >( domain.m_id ) );
				}
			}
			catch ( ... )
			{}
			m_domainWatcher->deleteLater();
			m_domainWatcher = nullptr;
		} );
}

void RecordTagWidget::onLoadRecord()
{
	if ( m_recordIdInput->text().trimmed().isEmpty() ) return;

	bool ok { false };
	const auto record_id = static_cast< idhan::RecordID >( m_recordIdInput->text().trimmed().toULongLong( &ok ) );
	if ( !ok || record_id == 0 ) return;

	m_currentRecordId = record_id;
	m_previewLabel->setText( "Loading..." );
	m_rawTagsList->clear();
	m_activeTagsList->clear();

	loadTags();
	fetchThumbnail();
}

void RecordTagWidget::loadTags()
{
	const auto domain_id = static_cast< idhan::TagDomainID >( m_domainCombo->currentData().toULongLong() );
	if ( domain_id == 0 ) return;
	m_currentDomainId = domain_id;

	auto& client { idhan::IDHANClient::instance() };

	{
		auto future { client.getRecordTags( m_currentRecordId, domain_id ) };
		m_rawTagsWatcher = new QFutureWatcher< std::vector< idhan::TagID > >( this );
		m_rawTagsWatcher->setFuture( future );

		connect(
			m_rawTagsWatcher,
			&QFutureWatcherBase::finished,
			this,
			[ this ]()
			{
				try
				{
					m_rawTagIds = m_rawTagsWatcher->result();
				}
				catch ( ... )
				{
					m_rawTagIds.clear();
				}
				m_rawTagsWatcher->deleteLater();
				m_rawTagsWatcher = nullptr;
				populateLists();
			} );
	}

	{
		auto future { client.getActiveRecordTags( m_currentRecordId, domain_id ) };
		m_activeTagsWatcher = new QFutureWatcher< std::vector< idhan::TagID > >( this );
		m_activeTagsWatcher->setFuture( future );

		connect(
			m_activeTagsWatcher,
			&QFutureWatcherBase::finished,
			this,
			[ this ]()
			{
				try
				{
					m_activeTagIds = m_activeTagsWatcher->result();
				}
				catch ( ... )
				{
					m_activeTagIds.clear();
				}
				m_activeTagsWatcher->deleteLater();
				m_activeTagsWatcher = nullptr;
				populateLists();
			} );
	}
}

void RecordTagWidget::populateLists()
{
	if ( m_rawTagsWatcher != nullptr || m_activeTagsWatcher != nullptr ) return;

	std::vector< idhan::TagID > all_ids;
	all_ids.reserve( m_rawTagIds.size() + m_activeTagIds.size() );
	all_ids.insert( all_ids.end(), m_rawTagIds.begin(), m_rawTagIds.end() );
	all_ids.insert( all_ids.end(), m_activeTagIds.begin(), m_activeTagIds.end() );

	if ( all_ids.empty() )
	{
		m_rawTagsList->clear();
		m_activeTagsList->clear();
		return;
	}

	auto& client { idhan::IDHANClient::instance() };
	auto future { client.getTagText( all_ids ) };

	m_tagTextWatcher = new QFutureWatcher< std::vector< std::string > >( this );
	m_tagTextWatcher->setFuture( future );

	connect(
		m_tagTextWatcher,
		&QFutureWatcherBase::finished,
		this,
		[ this, all_ids = std::move( all_ids ) ]()
		{
			try
			{
				const auto tag_texts { m_tagTextWatcher->result() };
				std::unordered_map< idhan::TagID, std::string > id_to_text;
				for ( std::size_t i = 0; i < all_ids.size() && i < tag_texts.size(); ++i )
				{
					id_to_text[ all_ids[ i ] ] = tag_texts[ i ];
				}

				m_rawTagsList->clear();
				for ( const auto tag_id : m_rawTagIds )
				{
					const auto it = id_to_text.find( tag_id );
					const auto text =
						( it != id_to_text.end() ) ? it->second : format_ns::format( "Unknown ({})", tag_id );
					auto* item = new QListWidgetItem( QString::fromStdString( text ), m_rawTagsList );
					item->setData( Qt::UserRole, static_cast< qlonglong >( tag_id ) );
				}

				m_activeTagsList->clear();
				for ( const auto tag_id : m_activeTagIds )
				{
					const auto it = id_to_text.find( tag_id );
					const auto text =
						( it != id_to_text.end() ) ? it->second : format_ns::format( "Unknown ({})", tag_id );
					auto* item = new QListWidgetItem( QString::fromStdString( text ), m_activeTagsList );
					item->setData( Qt::UserRole, static_cast< qlonglong >( tag_id ) );
				}
			}
			catch ( ... )
			{
				m_rawTagsList->clear();
				m_activeTagsList->clear();
			}
			m_tagTextWatcher->deleteLater();
			m_tagTextWatcher = nullptr;
		} );
}

void RecordTagWidget::fetchThumbnail()
{
	if ( m_currentRecordId == 0 ) return;

	auto& client { idhan::IDHANClient::instance() };
	auto base_url { client.getBaseUrl() };

	QUrl url { base_url };
	url.setPath( QString::fromStdString( format_ns::format( "/records/{}/thumbnail", m_currentRecordId ) ) );

	QNetworkRequest request { url };
	client.addKeyHeader( request );

	auto* reply { m_networkManager->get( request ) };

	connect(
		reply,
		&QNetworkReply::finished,
		this,
		[ this, reply ]()
		{
			if ( reply->error() == QNetworkReply::NoError )
			{
				QPixmap pixmap;
				if ( pixmap.loadFromData( reply->readAll() ) )
				{
					m_previewLabel->setPixmap(
						pixmap.scaled( 256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
				}
				else
				{
					m_previewLabel->setText( "Invalid image" );
				}
			}
			else
			{
				m_previewLabel->setText( "Failed to load" );
			}
			reply->deleteLater();
		} );
}

std::pair< std::string, std::string > RecordTagWidget::splitTagText( const std::string& tag_text )
{
	const auto colon_pos = tag_text.find( ':' );
	if ( colon_pos == std::string::npos || colon_pos == 0 )
	{
		return { "", tag_text };
	}
	return { tag_text.substr( 0, colon_pos ), tag_text.substr( colon_pos + 1 ) };
}

void RecordTagWidget::onAddTagTextChanged( const QString& text )
{
	if ( text.trimmed().isEmpty() )
	{
		m_autocompletePopup->hide();
		m_lastAutocompleteResults.clear();
		return;
	}

	if ( m_autocompleteWatcher != nullptr )
	{
		m_autocompleteWatcher->disconnect();
		m_autocompleteWatcher->cancel();
		m_autocompleteWatcher->deleteLater();
		m_autocompleteWatcher = nullptr;
	}

	auto& client { idhan::IDHANClient::instance() };
	auto future { client.autocompleteTag( text.trimmed() ) };

	m_autocompleteWatcher = new QFutureWatcher< std::vector< std::pair< idhan::TagID, std::string > > >( this );
	m_autocompleteWatcher->setFuture( future );

	connect( m_autocompleteWatcher, &QFutureWatcherBase::finished, this, &RecordTagWidget::onAutocompleteFinished );
}

void RecordTagWidget::onAutocompleteFinished()
{
	try
	{
		m_lastAutocompleteResults = m_autocompleteWatcher->result();
		m_autocompletePopup->clear();

		if ( m_lastAutocompleteResults.empty() )
		{
			m_autocompletePopup->hide();
			return;
		}

		for ( const auto& [ tag_id, tag_text ] : m_lastAutocompleteResults )
		{
			auto* item = new QListWidgetItem( QString::fromStdString( tag_text ), m_autocompletePopup );
			item->setData( Qt::UserRole, static_cast< qlonglong >( tag_id ) );
		}

		auto pos = m_addTagInput->mapToGlobal( QPoint( 0, m_addTagInput->height() ) );
		m_autocompletePopup->setMinimumWidth( m_addTagInput->width() );
		m_autocompletePopup->move( pos );
		m_autocompletePopup->show();
		m_autocompletePopup->setCurrentRow( 0 );
	}
	catch ( ... )
	{
		m_autocompletePopup->hide();
		m_lastAutocompleteResults.clear();
	}

	m_autocompleteWatcher->deleteLater();
	m_autocompleteWatcher = nullptr;
}

void RecordTagWidget::onAutocompleteItemClicked( QListWidgetItem* item )
{
	m_selectedAutocompleteTagId = static_cast< idhan::TagID >( item->data( Qt::UserRole ).toULongLong() );
	m_addTagInput->setText( item->text() );
	m_autocompletePopup->hide();
	onAddTag();
}

void RecordTagWidget::onAddTagReturnPressed()
{
	if ( m_autocompletePopup->isVisible() && m_autocompletePopup->currentItem() )
	{
		onAutocompleteItemClicked( m_autocompletePopup->currentItem() );
		return;
	}
	onAddTag();
}

void RecordTagWidget::onAddTag()
{
	if ( m_currentRecordId == 0 ) return;

	std::string tag_text;

	if ( m_selectedAutocompleteTagId != 0 )
	{
		tag_text = m_addTagInput->text().toStdString();
		m_selectedAutocompleteTagId = 0;
	}
	else if ( !m_lastAutocompleteResults.empty() )
	{
		tag_text = m_lastAutocompleteResults[ 0 ].second;
	}
	else
	{
		tag_text = m_addTagInput->text().trimmed().toStdString();
		if ( tag_text.empty() ) return;
	}

	m_autocompletePopup->hide();
	m_addTagInput->clear();

	const auto domain_id = m_currentDomainId;
	if ( domain_id == 0 ) return;

	auto pair = splitTagText( tag_text );

	auto& client { idhan::IDHANClient::instance() };

	std::vector< std::pair< std::string, std::string > > tags;
	tags.emplace_back( std::move( pair ) );

	auto future { client.addTags( m_currentRecordId, domain_id, std::move( tags ) ) };

	auto* watcher { new QFutureWatcher< void >( this ) };
	watcher->setFuture( future );

	connect(
		watcher,
		&QFutureWatcher< void >::finished,
		this,
		[ this, watcher ]()
		{
			loadTags();
			watcher->deleteLater();
		} );
}

void RecordTagWidget::onRemoveTag()
{
	if ( m_currentRecordId == 0 ) return;

	auto selected_items = m_rawTagsList->selectedItems();
	if ( selected_items.isEmpty() ) return;

	const auto domain_id = m_currentDomainId;
	if ( domain_id == 0 ) return;

	std::vector< idhan::TagID > tag_ids_to_remove;
	tag_ids_to_remove.reserve( selected_items.size() );

	for ( const auto* item : selected_items )
	{
		const auto tag_id = static_cast< idhan::TagID >( item->data( Qt::UserRole ).toULongLong() );
		tag_ids_to_remove.push_back( tag_id );
	}

	auto& client { idhan::IDHANClient::instance() };
	auto future { client.removeTags( m_currentRecordId, domain_id, tag_ids_to_remove ) };

	auto* watcher { new QFutureWatcher< void >( this ) };
	watcher->setFuture( future );

	connect(
		watcher,
		&QFutureWatcher< void >::finished,
		this,
		[ this, watcher ]()
		{
			loadTags();
			watcher->deleteLater();
		} );
}

void RecordTagWidget::onDetach()
{
	emit detachRequested();
}
