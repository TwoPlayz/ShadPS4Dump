#include "hook/qt_menu_hook.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include "hook/window_finder.h"
#include "install/pkg_installer.h"

namespace QtMenuHook {

namespace {

QString NormalizeMenuLabel(const QString& text) {
    QString out = text;
    out.remove(QLatin1Char('&'));
    return out;
}

QMenu* FindFileMenu(QMenuBar* bar) {
    if (!bar) {
        return nullptr;
    }

    for (QAction* action : bar->actions()) {
        if (!action || !action->menu()) {
            continue;
        }
        if (NormalizeMenuLabel(action->text()).compare(QLatin1String("File"),
                                                       Qt::CaseInsensitive) == 0) {
            return action->menu();
        }
    }

    const auto actions = bar->actions();
    if (!actions.isEmpty() && actions.front()->menu()) {
        return actions.front()->menu();
    }
    return nullptr;
}

bool MenuHasInstallAction(QMenu* file_menu) {
    for (QAction* action : file_menu->actions()) {
        if (NormalizeMenuLabel(action->text()).startsWith(QLatin1String("Install Game PKG"),
                                                          Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool LooksLikeRefreshAction(const QString& label) {
    const QString normalized = NormalizeMenuLabel(label);
    if (normalized.compare(QLatin1String("Refresh List"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (normalized.contains(QLatin1String("Refresh"), Qt::CaseInsensitive) &&
        normalized.contains(QLatin1String("List"), Qt::CaseInsensitive)) {
        return true;
    }
    if (normalized.contains(QLatin1String("Refresh"), Qt::CaseInsensitive) &&
        normalized.contains(QLatin1String("Game"), Qt::CaseInsensitive)) {
        return true;
    }
    return false;
}

bool TriggerRefreshOnWidget(QWidget* widget) {
    if (!widget) {
        return false;
    }

    if (QPushButton* button = widget->findChild<QPushButton*>(QStringLiteral("refreshButton"))) {
        button->click();
        return true;
    }

    const QList<QAction*> actions = widget->findChildren<QAction*>();
    for (QAction* action : actions) {
        if (action->objectName() == QLatin1String("refreshGameListAct")) {
            action->trigger();
            return true;
        }
    }

    for (QAction* action : actions) {
        if (LooksLikeRefreshAction(action->text())) {
            action->trigger();
            return true;
        }
    }

    return false;
}

void InstallOnMainWindow(QMainWindow* main_window) {
    if (!main_window) {
        return;
    }

    QMenu* file_menu = FindFileMenu(main_window->menuBar());
    if (!file_menu || MenuHasInstallAction(file_menu)) {
        return;
    }

    QAction* exit_action = nullptr;
    for (QAction* action : file_menu->actions()) {
        if (NormalizeMenuLabel(action->text()).compare(QLatin1String("Exit"),
                                                       Qt::CaseInsensitive) == 0) {
            exit_action = action;
            break;
        }
    }

    auto* install_action =
        new QAction(QStringLiteral("Install Game PKG..."), main_window);
    QObject::connect(install_action, &QAction::triggered, main_window, [main_window]() {
        PkgInstaller::RunInstallGameDialog(reinterpret_cast<HWND>(main_window->winId()));
    });

    if (exit_action) {
        file_menu->insertAction(exit_action, install_action);
        file_menu->insertSeparator(exit_action);
    } else {
        file_menu->addSeparator();
        file_menu->addAction(install_action);
    }
}

void InstallOnGuiThread() {
    QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) {
        return;
    }

    for (QWidget* widget : app->topLevelWidgets()) {
        if (auto* main_window = qobject_cast<QMainWindow*>(widget)) {
            InstallOnMainWindow(main_window);
        }
    }
}

void RefreshOnGuiThread() {
    QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) {
        return;
    }

    for (QWidget* widget : app->topLevelWidgets()) {
        if (TriggerRefreshOnWidget(widget)) {
            return;
        }
    }
}

void ScheduleRefreshOnGuiThread(int delay_ms) {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) {
        return;
    }

    QTimer::singleShot(delay_ms, app, RefreshOnGuiThread);
}

} // namespace

bool TryInstallMenuItem() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) {
        return false;
    }

    if (QThread::currentThread() == app->thread()) {
        InstallOnGuiThread();
        return true;
    }

    QTimer::singleShot(0, app, InstallOnGuiThread);
    return true;
}

void TriggerRefreshGameList() {
    ScheduleRefreshOnGuiThread(200);
}

} // namespace QtMenuHook
