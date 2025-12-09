cmake_minimum_required(VERSION 3.16)

# Nombre del proyecto
project(JtagScannerQt LANGUAGES CXX)

# Estándar C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configuración Qt Automática
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Buscar Qt6
find_package(Qt6 COMPONENTS Widgets Core Gui SerialPort REQUIRED)

# Incluir la carpeta src para que los #include funcionen
include_directories(src)

# --- BÚSQUEDA AUTOMÁTICA DE ARCHIVOS ---
# CORRECCIÓN: Añadido "src/*.ui" al final para que detecte mainwindow.ui
file(GLOB_RECURSE PROJECT_SOURCES "src/*.cpp" "src/*.h" "src/*.ui")

# --- CREAR EJECUTABLE ---
add_executable(JtagScannerQt WIN32 ${PROJECT_SOURCES})

# --- LINKADO ---
target_link_libraries(JtagScannerQt PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui Qt6::SerialPort)

# Corrección para Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(JtagScannerQt PRIVATE dl)
endif()