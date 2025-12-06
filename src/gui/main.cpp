#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // Set application information
    app.setApplicationName("TopJTAG Probe");
    app.setOrganizationName("YourCompany");
    app.setApplicationVersion("1.0.0");
    
    MainWindow mainWindow;
    mainWindow.show();
    
    return app.exec();
}
