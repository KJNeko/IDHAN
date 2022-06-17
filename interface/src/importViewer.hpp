//
// Created by kj16609 on 6/17/22.
//

#ifndef MAIN_IMPORTVIEWER_HPP
#define MAIN_IMPORTVIEWER_HPP

#include <QWidget>
#include <queue>
#include "MrMime/filetype_enum.h"


QT_BEGIN_NAMESPACE
namespace Ui { class ImportViewer; }
QT_END_NAMESPACE

class ImportViewer : public QWidget
{
Q_OBJECT

public:
	explicit ImportViewer( QWidget* parent = nullptr );
	
	~ImportViewer() override;
	
	void addFiles(const std::vector<std::pair<QString, MrMime::FileType>>& files);

private:
	Ui::ImportViewer* ui;
	
	//File import list
	std::queue<std::pair<QString, MrMime::FileType>> files;
	std::mutex filesMutex;
	
	//Record keeping
	uint64_t filesAdded {0};
	uint64_t filesProcessed {0};
};


#endif //MAIN_IMPORTVIEWER_HPP
