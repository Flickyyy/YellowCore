#include <QApplication>
#include <QStyleFactory>
#include "TcpClient.hpp"
#include "LoginWindow.hpp"
#include "MainWindow.hpp"

namespace {

const char* kStyleSheet = R"qss(
QMainWindow, QDialog {
    background: #f5f6f8;
}
QGroupBox {
    border: 1px solid #d0d4db;
    border-radius: 6px;
    margin-top: 10px;
    padding-top: 8px;
    font-weight: 600;
    color: #2c3e50;
    background: #ffffff;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 6px;
}
QTabWidget::pane {
    border: 1px solid #d0d4db;
    border-radius: 6px;
    background: #ffffff;
    top: -1px;
}
QTabBar::tab {
    background: #e9ecf1;
    color: #4a5566;
    border: 1px solid #d0d4db;
    border-bottom: none;
    padding: 7px 18px;
    margin-right: 2px;
    border-top-left-radius: 5px;
    border-top-right-radius: 5px;
    font-weight: 500;
}
QTabBar::tab:selected {
    background: #ffffff;
    color: #1f2937;
    border-bottom: 2px solid #2980b9;
}
QTabBar::tab:hover:!selected {
    background: #f0f2f6;
}
QTableWidget {
    border: 1px solid #d0d4db;
    border-radius: 4px;
    gridline-color: #e9ecf1;
    selection-background-color: #d6e8f5;
    selection-color: #1f2937;
    alternate-background-color: #fafbfc;
}
QHeaderView::section {
    background: #2c3e50;
    color: #ffffff;
    padding: 6px 8px;
    border: none;
    font-weight: 600;
}
QPushButton {
    background: #ffffff;
    border: 1px solid #c8cdd4;
    border-radius: 4px;
    padding: 6px 14px;
    color: #2c3e50;
    font-weight: 500;
}
QPushButton:hover  { background: #eef2f6; border-color: #2980b9; }
QPushButton:pressed{ background: #d6e8f5; }
QPushButton:disabled { color: #9ba3ad; background: #f5f6f8; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    border: 1px solid #c8cdd4;
    border-radius: 4px;
    padding: 4px 8px;
    background: #ffffff;
    selection-background-color: #d6e8f5;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: #2980b9;
}
QStatusBar {
    background: #ffffff;
    border-top: 1px solid #d0d4db;
    color: #2c3e50;
}
QLabel { color: #2c3e50; }
)qss";

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("YellowCore");
    app.setApplicationVersion("1.0");

    if (QStyleFactory::keys().contains("Fusion"))
        app.setStyle("Fusion");
    app.setStyleSheet(QString::fromUtf8(kStyleSheet));

    TcpClient client;
    LoginWindow login(&client);

    if (login.exec() != QDialog::Accepted)
        return 0;

    MainWindow window(&client, login.token(), login.username());
    window.show();

    return app.exec();
}
