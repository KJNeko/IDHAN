//
// Created by kj16609 on 6/17/22.
//

#ifndef MAIN_IMPORTVIEWER_HPP
#define MAIN_IMPORTVIEWER_HPP

#include <QWidget>
#include <queue>


QT_BEGIN_NAMESPACE
namespace Ui
{
class ImportViewer;
}
QT_END_NAMESPACE

class ImportViewer : public QWidget
{
	Q_OBJECT

  public:
	explicit ImportViewer( QWidget* parent = nullptr );

	~ImportViewer() override;

	void addFiles( const QVector<QPair<QString, QString>>& files );

  private:
	Ui::ImportViewer* ui;

	// File import list
	std::queue<QPair<QString, QString>> files;
	std::mutex filesMutex;

	// Record keeping
	uint64_t filesAdded { 0 };
	uint64_t filesProcessed { 0 };
};


#endif // MAIN_IMPORTVIEWER_HPP
