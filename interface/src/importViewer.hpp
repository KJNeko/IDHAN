//
// Created by kj16609 on 6/17/22.
//

#ifndef MAIN_IMPORTVIEWER_HPP
#define MAIN_IMPORTVIEWER_HPP

#include <QFuture>
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

	void processFiles();

	void addFiles( const std::vector<std::pair<std::string, std::string>>& files );

  private:
	Ui::ImportViewer* ui;

	// File import list
	std::vector<std::pair<std::string, std::string>> files;
	QFuture<void>* processingThread { nullptr };

	// Record keeping
	uint64_t filesAdded { 0 };
	uint64_t filesProcessed { 0 };

  private slots:
	void updateValues_slot();

  signals:
	void updateValues();
};


#endif // MAIN_IMPORTVIEWER_HPP
