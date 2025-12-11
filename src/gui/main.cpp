#include "mainwindow.h"

#include <QApplication>
#include "../controller/ScanWorker.h"  // Para acceder a JTAG::PinLevel

int main(int argc, char *argv[])
{
    // Registrar tipos personalizados para señales Qt cross-thread
    qRegisterMetaType<std::vector<JTAG::PinLevel>>("std::vector<JTAG::PinLevel>");

    QApplication app(argc, argv);
    
    // Set application information
    app.setApplicationName("TopJTAG Probe");
    app.setOrganizationName("YourCompany");
    app.setApplicationVersion("1.0.0");
    
    MainWindow mainWindow;
    mainWindow.show();
    
    return app.exec();
}
