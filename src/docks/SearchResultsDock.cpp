/*
 * This file is part of Notepad Next.
 * Copyright 2022 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */


#include "ApplicationSettings.h"
#include "NotepadNextApplication.h"
#include "SearchResultHighlighterDelegate.h"
#include "SearchResultData.h"
#include "SearchResultsDock.h"
#include "ScintillaNext.h"
#include "ui_SearchResultsDock.h"

#include <QKeyEvent>
#include <QWheelEvent>
#include <QPointer>
#include <QMenu>
#include <QShortcut>
#include <QClipboard>


SearchResultsDock::SearchResultsDock(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::SearchResultsDock)
{
    ui->setupUi(this);

    // Close the results when escape is pressed
    new QShortcut(QKeySequence::Cancel, this, this, &SearchResultsDock::close, Qt::WidgetWithChildrenShortcut);

    // Keep Ctrl+A for the results instead of the window level "Select All" of the editor
    ui->treeWidget->installEventFilter(this);
    ui->treeWidget->viewport()->installEventFilter(this);

    connect(ui->treeWidget, &QTreeWidget::itemActivated, this, &SearchResultsDock::itemActivated);
    connect(ui->treeWidget, &QTreeWidget::itemExpanded, this, &SearchResultsDock::itemExpanded);

    connect(ui->treeWidget, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = ui->treeWidget->itemAt(pos);

        if (item == Q_NULLPTR) {
            return;
        }

        // Create the menu and show it
        QMenu menu(this);
        menu.addAction(tr("Copy"), this, &SearchResultsDock::copySelectedSearchResultsToClipboard);
        menu.addSeparator();
        menu.addAction(tr("Collapse All"), this, &SearchResultsDock::collapseAll);
        menu.addAction(tr("Expand All"), this, &SearchResultsDock::expandAll);
        menu.addSeparator();
        menu.addAction(tr("Delete Entry"), this, [=, this]() { deleteEntry(item); });
        menu.addSeparator();
        menu.addAction(tr("Delete All"), this, &SearchResultsDock::deleteAll);

        menu.exec(QCursor::pos());
    });

    ui->treeWidget->setItemDelegate(new SearchResultHighlighterDelegate(ui->treeWidget));

    ApplicationSettings *settings = qobject_cast<NotepadNextApplication*>(qApp)->getSettings();
    connect(settings, &ApplicationSettings::fontNameChanged, this, &SearchResultsDock::updateTreeWidgetFont);
    connect(settings, &ApplicationSettings::fontSizeChanged, this, &SearchResultsDock::updateTreeWidgetFont);
    updateTreeWidgetFont();
}

void SearchResultsDock::updateTreeWidgetFont()
{
    ApplicationSettings *settings = qobject_cast<NotepadNextApplication*>(qApp)->getSettings();

    QFont f(settings->fontName(), qBound(4, settings->fontSize() + zoomOffset, 72));
    ui->treeWidget->setFont(f);
    ui->treeWidget->resizeColumnToContents(0);
}

void SearchResultsDock::zoomResults(int delta)
{
    // 0 means reset
    zoomOffset = delta == 0 ? 0 : zoomOffset + delta;
    updateTreeWidgetFont();
}

SearchResultsDock::~SearchResultsDock()
{
    delete ui;
}

// Returns +1 for zoom in, -1 for zoom out, 0 for reset, or -100 when it is not a zoom key
static int zoomKeyDelta(const QKeyEvent *ke)
{
    if (!(ke->modifiers() & Qt::ControlModifier)) {
        return -100;
    }

    switch (ke->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        return 1;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        return -1;
    case Qt::Key_0:
        return 0;
    default:
        return -100;
    }
}

bool SearchResultsDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->treeWidget && event->type() == QEvent::ShortcutOverride) {
        // Accepting this makes the key press go to the tree, rather than triggering the (window wide)
        // shortcut of the main window which would act on the editors
        const QKeyEvent *ke = static_cast<QKeyEvent *>(event);

        if (ke->matches(QKeySequence::SelectAll) || zoomKeyDelta(ke) != -100) {
            event->accept();
            return true;
        }
    }
    else if (watched == ui->treeWidget && event->type() == QEvent::KeyPress) {
        const int delta = zoomKeyDelta(static_cast<QKeyEvent *>(event));

        if (delta != -100) {
            zoomResults(delta);
            return true;
        }
    }
    else if (watched == ui->treeWidget->viewport() && event->type() == QEvent::Wheel) {
        QWheelEvent *we = static_cast<QWheelEvent *>(event);

        if ((we->modifiers() & Qt::ControlModifier) && we->angleDelta().y() != 0) {
            zoomResults(we->angleDelta().y() > 0 ? 1 : -1);
            return true;
        }
    }

    return QDockWidget::eventFilter(watched, event);
}

bool SearchResultsDock::isDark() const
{
    return ui->treeWidget->palette().color(QPalette::Base).lightness() < 128;
}

void SearchResultsDock::newSearch(const QString searchTerm)
{
    show();

    this->searchTerm = searchTerm;

    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); ++i)
    {
        const QTreeWidgetItem* topLevelItem = ui->treeWidget->topLevelItem(i);
        ui->treeWidget->collapseItem(topLevelItem);
    }

    currentSearch = new QTreeWidgetItem();
    ui->treeWidget->insertTopLevelItem(0, currentSearch);

    currentSearch->setBackground(0, isDark() ? QColor(38, 38, 72) : QColor(232, 232, 255));
    currentSearch->setForeground(0, isDark() ? QColor(140, 170, 255) : QColor(0, 0, 170));
    currentSearch->setExpanded(true);
    currentSearch->setFirstColumnSpanned(true);

    updateSearchStatus();
}

void SearchResultsDock::newFileEntry(ScintillaNext *editor)
{
    // Store a QPointer since there is no guarantee this editor will be around later
    QPointer<ScintillaNext> editor_pointer = editor;

    totalFileHitCount = 0;
    currentFilePath = editor->isFile() ? editor->getFilePath() : editor->getName();

    currentFile = new QTreeWidgetItem(currentSearch);
    currentFile->setData(0, Qt::UserRole, QVariant::fromValue(editor_pointer));

    currentFile->setBackground(0, isDark() ? QColor(30, 62, 30) : QColor(213, 255, 213));
    currentFile->setForeground(0, isDark() ? QColor(120, 210, 120) : QColor(0, 128, 0));
    currentFile->setExpanded(true);
    currentFile->setFirstColumnSpanned(true);

    currentFileCount++;
    updateSearchStatus();
}

void SearchResultsDock::newResultsEntry(const QString line, int lineNumber, int startPositionFromBeginning, int endPositionFromBeginning, int hitCount)
{
    QTreeWidgetItem *item = new QTreeWidgetItem(currentFile);

    // Scintilla internally references line numbers starting at 0, however it needs displayed starting at 1
    item->setText(0, QString::number(lineNumber + 1));
    item->setBackground(0, QBrush(isDark() ? QColor(50, 50, 50) : QColor(220, 220, 220)));
    item->setTextAlignment(0, Qt::AlignRight);

    item->setData(1, SearchResultData::LineNumber, lineNumber);
    item->setData(1, SearchResultData::LinePosStart, startPositionFromBeginning);
    item->setData(1, SearchResultData::LinePosEnd, endPositionFromBeginning);
    item->setData(1, Qt::UserRole + 3, true); // <- Flag to enable highlight
    item->setText(1, line);

    totalFileHitCount += hitCount;
    totalHitCount += hitCount;

    updateSearchStatus();
}

void SearchResultsDock::completeSearch()
{
    currentSearch = Q_NULLPTR;
    currentFile = Q_NULLPTR;
    currentFileCount = 0;
    totalFileHitCount = 0;
    totalHitCount = 0;

    ui->treeWidget->resizeColumnToContents(0);
    ui->treeWidget->resizeColumnToContents(1);
}

void SearchResultsDock::collapseAll() const
{
    ui->treeWidget->collapseAll();
}

void SearchResultsDock::expandAll() const
{
    ui->treeWidget->expandAll();
}

void SearchResultsDock::deleteEntry(QTreeWidgetItem *item)
{
    QTreeWidgetItem *parent = item->parent();

    if (parent != Q_NULLPTR) {
        parent->removeChild(item);
        delete item;
    }
    else {
        const int index = ui->treeWidget->indexOfTopLevelItem(item);
        delete ui->treeWidget->takeTopLevelItem(index);
    }
}

void SearchResultsDock::deleteAll()
{
    ui->treeWidget->clear();
}

void SearchResultsDock::itemActivated(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    // Result entries have no children
    // Make sure the entry has a parent since search entries can have no children
    if (item->childCount() == 0 && item->parent() != Q_NULLPTR) {
        QPointer<ScintillaNext> editor = item->parent()->data(0, Qt::UserRole).value<QPointer<ScintillaNext>>();

        // The editor may no longer exist
        if (editor) {
            int lineNumber = item->data(1, SearchResultData::LineNumber).toInt();
            int startPositionFromBeginning = item->data(1, SearchResultData::LinePosStart).toInt();
            int endPositionFromBeginning = item->data(1, SearchResultData::LinePosEnd).toInt();

            emit searchResultActivated(editor, lineNumber, startPositionFromBeginning, endPositionFromBeginning);
        }
    }
}

void SearchResultsDock::itemExpanded(QTreeWidgetItem *)
{
    ui->treeWidget->resizeColumnToContents(1);
}

void SearchResultsDock::updateSearchStatus()
{
    currentSearch->setText(0, QStringLiteral("Search \"%1\" (%L2 hits in %L3 files)").arg(searchTerm).arg(totalHitCount).arg(currentFileCount));

    if (currentFile)
        currentFile->setText(0, QStringLiteral("%1 (%L2 hits)").arg(currentFilePath).arg(totalFileHitCount));
}

void SearchResultsDock::copyAllSearchResultsToClipboard()
{
    QStringList results;
    QTreeWidgetItemIterator it(ui->treeWidget);

    while (*it) {
        const QTreeWidgetItem *item = *it;
        results.append(QStringLiteral("%1 %2").arg(item->text(0), item->text(1)));
        ++it;
    }

    QGuiApplication::clipboard()->setText(results.join('\n'));
}

void SearchResultsDock::copySelectedSearchResultsToClipboard()
{
    QStringList results;
    QTreeWidgetItemIterator it(ui->treeWidget);

    while (*it) {
        const QTreeWidgetItem *item = *it;

        if (item->isSelected()) {
            results.append(QStringLiteral("%1 %2").arg(item->text(0), item->text(1)));
        }

        ++it;
    }

    QGuiApplication::clipboard()->setText(results.join('\n'));
}
