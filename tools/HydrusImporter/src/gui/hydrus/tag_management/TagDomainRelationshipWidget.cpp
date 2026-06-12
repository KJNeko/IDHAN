#include "TagDomainRelationshipWidget.hpp"
#include "ui_TagDomainRelationshipWidget.h"
#include <QInputDialog>
#include <QMessageBox>

TagDomainRelationshipWidget::TagDomainRelationshipWidget(idhan::TagDomainID domainID, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TagDomainRelationshipWidget),
    m_domainID(domainID),
    m_aliasTargetWatcher(new QFutureWatcher<std::vector<std::pair<idhan::TagID, std::string>>>(this))
{
    ui->setupUi(this);

    connect(m_aliasTargetWatcher, &QFutureWatcher<std::vector<std::pair<idhan::TagID, std::string>>>::finished, this, [this]() {
        ui->aliasTargetResults->clear();
        const auto results = m_aliasTargetWatcher->result();
        for (const auto& [id, tagText] : results) {
            auto item = new QListWidgetItem(QString::fromStdString(tagText), ui->aliasTargetResults);
            item->setData(Qt::UserRole, QVariant::fromValue(id));
        }
    });
}

TagDomainRelationshipWidget::~TagDomainRelationshipWidget() {
    delete ui;
}

void TagDomainRelationshipWidget::setTag(idhan::TagID tagID) {
    m_tagID = tagID;
    if (m_aliasSearchMode) exitAliasSearchMode();
    updateRelationships();
}

void TagDomainRelationshipWidget::clearLists() {
    ui->parentsList->clear();
    ui->childrenList->clear();
    ui->olderSiblingsList->clear();
    ui->youngerSiblingsList->clear();
    ui->aliasedByList->clear();
}

void TagDomainRelationshipWidget::updateRelationships() {
    clearLists();
    if (m_tagID == 0 || m_domainID == 0) return;
    auto future = idhan::IDHANClient::instance().getTagRelationships(m_tagID, m_domainID);
    auto watcher = new QFutureWatcher<idhan::TagRelationshipInfo>(this);
    connect(watcher, &QFutureWatcher<idhan::TagRelationshipInfo>::finished, this, [this, watcher]() {
        auto info = watcher->result();

        ui->label_parents->setText(QString("Parents (%1):").arg(info.m_parents.size()));
        ui->label_children->setText(QString("Children (%1):").arg(info.m_children.size()));
        ui->label_olderSiblings->setText(QString("Older Siblings (%1):").arg(info.m_older_siblings.size()));
        ui->label_youngerSiblings->setText(QString("Younger Siblings (%1):").arg(info.m_younger_siblings.size()));
        ui->label_aliasedBy->setText(QString("Aliased by (%1):").arg(info.m_aliased.size()));

        auto& client = idhan::IDHANClient::instance();

        auto fetchAndPopulate = [this, &client](std::vector<idhan::TagID> ids, QListWidget* list) {
            if (ids.empty()) return;
            auto textsFuture = client.getTagText(ids);
            auto textsWatcher = new QFutureWatcher<std::vector<std::string>>(this);
            connect(textsWatcher, &QFutureWatcher<std::vector<std::string>>::finished, this, [this, textsWatcher, ids = std::move(ids), list]() {
                auto texts = textsWatcher->result();
                for (size_t i = 0; i < texts.size(); ++i) {
                    auto item = new QListWidgetItem(QString::fromStdString(texts[i]), list);
                    item->setData(Qt::UserRole, QVariant::fromValue(ids[i]));
                }
                textsWatcher->deleteLater();
            });
            textsWatcher->setFuture(textsFuture);
        };

        fetchAndPopulate(info.m_parents, ui->parentsList);
        fetchAndPopulate(info.m_children, ui->childrenList);
        fetchAndPopulate(info.m_older_siblings, ui->olderSiblingsList);
        fetchAndPopulate(info.m_younger_siblings, ui->youngerSiblingsList);
        fetchAndPopulate(info.m_aliased, ui->aliasedByList);

        if (!info.m_aliases.empty()) {
            auto textFuture = client.getTagText(info.m_aliases[0]);
            auto textWatcher = new QFutureWatcher<std::string>(this);
            connect(textWatcher, &QFutureWatcher<std::string>::finished, this, [this, textWatcher]() {
                auto text = textWatcher->result();
                ui->aliasTargetEdit->setText(QString::fromStdString(text));
                textWatcher->deleteLater();
            });
            textWatcher->setFuture(textFuture);
        } else {
            ui->aliasTargetEdit->setText(QString());
        }

        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void TagDomainRelationshipWidget::on_setAliasTargetButton_clicked() {
    if (!m_aliasSearchMode) {
        enterAliasSearchMode();
    } else {
        exitAliasSearchMode();
    }
}

void TagDomainRelationshipWidget::enterAliasSearchMode() {
    if (m_tagID == 0) return;

    m_aliasSearchMode = true;
    ui->aliasTargetEdit->setReadOnly(false);
    ui->aliasTargetEdit->clear();
    ui->aliasTargetEdit->setPlaceholderText("Search for alias target tag...");
    ui->aliasTargetEdit->setFocus();
    ui->setAliasTargetButton->setText("Cancel");
    ui->aliasTargetResults->show();
}

void TagDomainRelationshipWidget::exitAliasSearchMode() {
    m_aliasSearchMode = false;
    ui->aliasTargetEdit->setReadOnly(true);
    ui->aliasTargetEdit->setPlaceholderText("NOT SET");
    ui->setAliasTargetButton->setText("Set Alias Target");
    ui->aliasTargetResults->clear();
    ui->aliasTargetResults->hide();
    updateAliasTargetDisplay();
}

void TagDomainRelationshipWidget::updateAliasTargetDisplay() {
    if (m_tagID == 0 || m_domainID == 0) {
        ui->aliasTargetEdit->setText(QString());
        return;
    }

    auto future = idhan::IDHANClient::instance().getTagRelationships(m_tagID, m_domainID);
    auto watcher = new QFutureWatcher<idhan::TagRelationshipInfo>(this);
    connect(watcher, &QFutureWatcher<idhan::TagRelationshipInfo>::finished, this, [this, watcher]() {
        auto info = watcher->result();
        if (info.m_aliases.empty()) {
            ui->aliasTargetEdit->setText(QString());
        } else {
            auto& client = idhan::IDHANClient::instance();
            auto textFuture = client.getTagText(info.m_aliases[0]);
            auto textWatcher = new QFutureWatcher<std::string>(this);
            connect(textWatcher, &QFutureWatcher<std::string>::finished, this, [this, textWatcher]() {
                auto text = textWatcher->result();
                ui->aliasTargetEdit->setText(QString::fromStdString(text));
                textWatcher->deleteLater();
            });
            textWatcher->setFuture(textFuture);
        }
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void TagDomainRelationshipWidget::on_aliasTargetEdit_textChanged(const QString &text) {
    if (!m_aliasSearchMode) return;

    if (text.length() < 2) {
        ui->aliasTargetResults->clear();
        return;
    }

    if (m_aliasTargetWatcher->isRunning()) {
        m_aliasTargetWatcher->cancel();
    }

    auto future = idhan::IDHANClient::instance().autocompleteTag(text);
    m_aliasTargetWatcher->setFuture(future);
}

void TagDomainRelationshipWidget::on_aliasTargetResults_itemClicked(QListWidgetItem *item) {
    if (!m_aliasSearchMode) return;

    auto idVariant = item->data(Qt::UserRole);
    if (!idVariant.isValid()) return;

    auto chosenTagID = idVariant.value<idhan::TagID>();

    auto& client = idhan::IDHANClient::instance();
    auto future = client.createAliasRelationship(m_domainID, m_tagID, chosenTagID);
    auto watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        updateRelationships();
        watcher->deleteLater();
    });
    watcher->setFuture(future);

    exitAliasSearchMode();
}

// Relationship add/remove buttons

void TagDomainRelationshipWidget::on_addParentButton_clicked() {
    if (m_tagID == 0 || m_domainID == 0) return;
    bool ok;
    QString text = QInputDialog::getText(this, "Add Parent", "Parent Tag:", QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty()) {
        auto& client = idhan::IDHANClient::instance();
        auto future = client.createTag(text.toStdString());
        auto watcher = new QFutureWatcher<idhan::TagID>(this);
        connect(watcher, &QFutureWatcher<idhan::TagID>::finished, this, [this, watcher, &client]() {
            idhan::TagID newID = watcher->result();
            client.createParentRelationship(m_domainID, newID, m_tagID);
            updateRelationships();
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    }
}

void TagDomainRelationshipWidget::on_removeParentButton_clicked() {
    auto items = ui->parentsList->selectedItems();
    if (items.isEmpty()) return;
    auto idVariant = items[0]->data(Qt::UserRole);
    if (!idVariant.isValid()) return;
    auto& client = idhan::IDHANClient::instance();
    auto future = client.removeParentRelationship(m_domainID, idVariant.value<idhan::TagID>(), m_tagID);
    auto watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        updateRelationships();
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void TagDomainRelationshipWidget::on_addChildButton_clicked() {
    if (m_tagID == 0 || m_domainID == 0) return;
    bool ok;
    QString text = QInputDialog::getText(this, "Add Child", "Child Tag:", QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty()) {
        auto& client = idhan::IDHANClient::instance();
        auto future = client.createTag(text.toStdString());
        auto watcher = new QFutureWatcher<idhan::TagID>(this);
        connect(watcher, &QFutureWatcher<idhan::TagID>::finished, this, [this, watcher, &client]() {
            idhan::TagID newID = watcher->result();
            client.createParentRelationship(m_domainID, m_tagID, newID);
            updateRelationships();
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    }
}

void TagDomainRelationshipWidget::on_removeChildButton_clicked() {
    auto items = ui->childrenList->selectedItems();
    if (items.isEmpty()) return;
    auto idVariant = items[0]->data(Qt::UserRole);
    if (!idVariant.isValid()) return;
    auto& client = idhan::IDHANClient::instance();
    auto future = client.removeParentRelationship(m_domainID, m_tagID, idVariant.value<idhan::TagID>());
    auto watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        updateRelationships();
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void TagDomainRelationshipWidget::on_addOlderSiblingButton_clicked() {
    if (m_tagID == 0 || m_domainID == 0) return;
    bool ok;
    QString text = QInputDialog::getText(this, "Add Older Sibling", "Older Sibling Tag:", QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty()) {
        auto& client = idhan::IDHANClient::instance();
        auto future = client.createTag(text.toStdString());
        auto watcher = new QFutureWatcher<idhan::TagID>(this);
        connect(watcher, &QFutureWatcher<idhan::TagID>::finished, this, [this, watcher, &client]() {
            idhan::TagID newID = watcher->result();
            client.createSiblingRelationship(m_domainID, newID, m_tagID);
            updateRelationships();
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    }
}

void TagDomainRelationshipWidget::on_removeOlderSiblingButton_clicked() {
    auto items = ui->olderSiblingsList->selectedItems();
    if (items.isEmpty()) return;
    auto idVariant = items[0]->data(Qt::UserRole);
    if (!idVariant.isValid()) return;
    auto& client = idhan::IDHANClient::instance();
    auto future = client.removeSiblingRelationship(m_domainID, idVariant.value<idhan::TagID>(), m_tagID);
    auto watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        updateRelationships();
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void TagDomainRelationshipWidget::on_addYoungerSiblingButton_clicked() {
    if (m_tagID == 0 || m_domainID == 0) return;
    bool ok;
    QString text = QInputDialog::getText(this, "Add Younger Sibling", "Younger Sibling Tag:", QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty()) {
        auto& client = idhan::IDHANClient::instance();
        auto future = client.createTag(text.toStdString());
        auto watcher = new QFutureWatcher<idhan::TagID>(this);
        connect(watcher, &QFutureWatcher<idhan::TagID>::finished, this, [this, watcher, &client]() {
            idhan::TagID newID = watcher->result();
            client.createSiblingRelationship(m_domainID, m_tagID, newID);
            updateRelationships();
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    }
}

void TagDomainRelationshipWidget::on_removeYoungerSiblingButton_clicked() {
    auto items = ui->youngerSiblingsList->selectedItems();
    if (items.isEmpty()) return;
    auto idVariant = items[0]->data(Qt::UserRole);
    if (!idVariant.isValid()) return;
    auto& client = idhan::IDHANClient::instance();
    auto future = client.removeSiblingRelationship(m_domainID, m_tagID, idVariant.value<idhan::TagID>());
    auto watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        updateRelationships();
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void TagDomainRelationshipWidget::on_addAliasedByButton_clicked() {
    if (m_tagID == 0 || m_domainID == 0) return;
    bool ok;
    QString text = QInputDialog::getText(this, "Add Alias Source", "Tag to alias to this:", QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty()) {
        auto& client = idhan::IDHANClient::instance();
        auto future = client.createTag(text.toStdString());
        auto watcher = new QFutureWatcher<idhan::TagID>(this);
        connect(watcher, &QFutureWatcher<idhan::TagID>::finished, this, [this, watcher, &client]() {
            idhan::TagID newID = watcher->result();
            client.createAliasRelationship(m_domainID, newID, m_tagID);
            updateRelationships();
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    }
}

void TagDomainRelationshipWidget::on_removeAliasedByButton_clicked() {
    auto items = ui->aliasedByList->selectedItems();
    if (items.isEmpty()) return;
    auto idVariant = items[0]->data(Qt::UserRole);
    if (!idVariant.isValid()) return;
    auto& client = idhan::IDHANClient::instance();
    auto future = client.removeAliasRelationship(m_domainID, idVariant.value<idhan::TagID>(), m_tagID);
    auto watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        updateRelationships();
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}
