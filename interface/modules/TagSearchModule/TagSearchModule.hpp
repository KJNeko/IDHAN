//
// Created by kj16609 on 7/26/22.
//

#ifndef IDHAN_TAGSEARCHMODULE_HPP
#define IDHAN_TAGSEARCHMODULE_HPP


#include <QWidget>

#include "DatabaseModule/DatabaseObjects/database.hpp"


QT_BEGIN_NAMESPACE
namespace Ui
{
	class TagSearchModule;
}
QT_END_NAMESPACE

class TagSearchModule : public QWidget
{
Q_OBJECT

public:
	explicit TagSearchModule( QWidget* parent = nullptr );

	~TagSearchModule() override;

private:
	Ui::TagSearchModule* ui;

	pqxx::result result;
	std::mutex result_lock {};

	void updateTagSearch();

signals:

	void updateSearchResults( const pqxx::result res );

	void searchComplete( const std::vector< uint64_t >& file_ids );

	void updateFileList( const pqxx::result res );


private slots:

	void on_searchBar_textChanged( const QString& text );

	void on_searchBar_returnPressed();

	void on_searchResults_doubleClicked();

	void on_activeTags_doubleClicked();

	void updateSearch( const pqxx::result& res );
};


#endif //IDHAN_TAGSEARCHMODULE_HPP
