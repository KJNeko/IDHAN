//
// Created by kj16609 on 5/2/25.
//
// You may need to build the project (run Qt uic code generator) to get "ui_MainWindow.h" resolved

#include "MainWindow.hpp"

#include <QCompleter>
#include <QListView>
#include <QStringListModel>

#include "TagNode.hpp"
#include "ui_MainWindow.h"

MainWindow::MainWindow( QWidget* parent ) : QMainWindow( parent ), ui( new Ui::MainWindow )
{
	ui->setupUi( this );

	// TagNode* node { new TagNode( ui->graphicsView ) };

	// ui->graphicsView->scene()->addItem( node );
	ui->tagSearch->setCompleter( &m_completer );
	m_completer.setCaseSensitivity( Qt::CaseInsensitive );
	m_completer.setCompletionMode( QCompleter::UnfilteredPopupCompletion );
	m_completer.setModel( m_model.get() );

	connect(
		&m_autocomplete_watcher,
		&QFutureWatcher< std::vector< std::pair< idhan::TagID, std::string > > >::finished,
		this,
		&MainWindow::autocompleteFinished );

	connect(
		&m_completer, QOverload< const QString& >::of( &QCompleter::activated ), ui->tagSearch, &QLineEdit::setText );
}

MainWindow::~MainWindow()
{
	delete ui;
}

void MainWindow::on_tagSearch_textChanged( const QString& text )
{
	const auto tags { m_client.autocompleteTag( text ) };

	m_autocomplete_watcher.setFuture( tags );

	idhan::logging::debug( "Searching for tags matching \"{}\"", text.toStdString() );
}

void MainWindow::on_tagSearch_returnPressed()
{
	const auto tag_name { ui->tagSearch->text() };

	idhan::logging::debug( "Searching for tag: {}", tag_name.toStdString() );

	const auto itter { tag_map.find( tag_name.toStdString() ) };

	if ( itter != tag_map.end() )
	{
		auto* root { new TagNode( ui->graphicsView, itter->second ) };

		ui->graphicsView->setRootNode( root );
	}
	else
	{
		ui->tagSearch->setText( "" );
	}
}

void MainWindow::autocompleteFinished()
{
	idhan::logging::debug( "Autocomplete finished" );

	QStringList list {};

	const auto& future { m_autocomplete_watcher.future() };

	for ( const auto& tag : future.result() )
	{
		const auto [ tag_id, string ] = tag;
		list.append( QString::fromStdString( string ) );
		tag_map.emplace( string, tag_id );
	}

	idhan::logging::debug( "Got {} tags", future.result().size() );

	m_model->setStringList( list );
}
