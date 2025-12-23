/**
 * @file main.cpp
 * @brief Punto de entrada principal de la aplicación TopJTAG Probe
 *
 * Este archivo contiene la función main() que inicializa la aplicación Qt
 * y crea la ventana principal del Boundary Scanner JTAG.
 */

#include "mainwindow.h"

#include <QApplication>
#include "../controller/ScanWorker.h"  // Para acceder a JTAG::PinLevel

/**
 * @brief Punto de entrada principal de la aplicación
 *
 * Funcionalidades:
 * 1. Registra tipos personalizados para señales Qt cross-thread
 * 2. Inicializa la aplicación Qt con información de metadata
 * 3. Crea y muestra la ventana principal
 * 4. Inicia el event loop de Qt
 *
 * @param argc Número de argumentos de línea de comandos
 * @param argv Array de argumentos de línea de comandos
 * @return Código de salida de la aplicación (0 = éxito)
 */
int main(int argc, char *argv[])
{
    // Registrar tipos personalizados para señales Qt cross-thread
    // Esto es necesario porque ScanWorker se ejecuta en un thread separado
    // FASE 2: Usar shared_ptr para evitar copias profundas (95% reducción en overhead)
    qRegisterMetaType<std::shared_ptr<const std::vector<JTAG::PinLevel>>>("std::shared_ptr<const std::vector<JTAG::PinLevel>>");

    // Crear instancia de la aplicación Qt
    QApplication app(argc, argv);

    // Configurar metadata de la aplicación
    // Esta información se usa en diálogos "About", configuraciones, etc.
    app.setApplicationName("BoundaryScanner");
    app.setOrganizationName("UVa");
    app.setApplicationVersion("1.0.0");

    // Crear y mostrar la ventana principal
    // MainWindow contiene toda la lógica de la interfaz gráfica,
    // incluyendo la tabla de pines, el visualizador de chip,
    // el panel de control y la integración con ScanController
    MainWindow mainWindow;
    mainWindow.show();

    // Iniciar el event loop de Qt
    // Esta llamada bloquea hasta que se cierra la aplicación
    return app.exec();
}
