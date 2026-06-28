#include "hook/qt_launcher_util.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QBoxLayout>
#include <QLayout>
#include <QMainWindow>
#include <QSplitter>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace QtLauncherUtil {

namespace {

QString ReadTableCellText(QTableWidget* table, int row, int column) {
    if (!table || row < 0 || column < 0) {
        return {};
    }

    if (QWidget* widget = table->cellWidget(row, column)) {
        if (auto* label = widget->findChild<QLabel*>()) {
            return label->text().trimmed();
        }
    }

    if (QTableWidgetItem* item = table->item(row, column)) {
        return item->text().trimmed();
    }
    return {};
}

QString SerialFromGameList(QTableWidget* table) {
    if (!table || !table->isVisible()) {
        return {};
    }
    const int row = table->currentRow();
    if (row < 0) {
        return {};
    }
    return ReadTableCellText(table, row, 3);
}

} // namespace

QMainWindow* FindMainWindow() {
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        for (QWidget* widget : app->topLevelWidgets()) {
            if (auto* main_window = qobject_cast<QMainWindow*>(widget)) {
                return main_window;
            }
        }
    }
    return nullptr;
}

QSplitter* FindMainSplitter(QMainWindow* main_window) {
    if (!main_window) {
        return nullptr;
    }

    if (auto* log = main_window->findChild<QTextEdit*>("logDisplay")) {
        if (auto* splitter = qobject_cast<QSplitter*>(log->parentWidget())) {
            return splitter;
        }
    }

    for (QSplitter* splitter : main_window->findChildren<QSplitter*>()) {
        if (splitter->orientation() == Qt::Vertical && splitter->count() >= 2) {
            return splitter;
        }
    }
    return nullptr;
}

bool AttachBelowSplitter(QSplitter* splitter, QWidget* widget) {
    if (!splitter || !widget) {
        return false;
    }

    QWidget* host = splitter->parentWidget();
    if (!host) {
        return false;
    }

    QLayout* layout = host->layout();
    if (!layout) {
        return false;
    }

    auto* box_layout = qobject_cast<QBoxLayout*>(layout);
    if (!box_layout) {
        return false;
    }

    const int splitter_index = layout->indexOf(splitter);
    if (splitter_index < 0) {
        return false;
    }

    widget->setParent(host);
    box_layout->insertWidget(splitter_index + 1, widget);
    return true;
}

QString SelectedGameSerial(QMainWindow* main_window) {
    if (!main_window) {
        return {};
    }

    if (auto* list = main_window->findChild<QTableWidget*>("gamelist")) {
        const QString serial = SerialFromGameList(list);
        if (!serial.isEmpty()) {
            return serial;
        }
    }

    return {};
}

} // namespace QtLauncherUtil
