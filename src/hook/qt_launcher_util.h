#pragma once

class QMainWindow;
class QSplitter;
class QString;
class QWidget;

namespace QtLauncherUtil {

QMainWindow* FindMainWindow();

QSplitter* FindMainSplitter(QMainWindow* main_window);

bool AttachBelowSplitter(QSplitter* splitter, QWidget* widget);

QString SelectedGameSerial(QMainWindow* main_window);

} // namespace QtLauncherUtil
