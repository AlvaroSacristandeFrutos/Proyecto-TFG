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
find_package(Qt6 COMPONENTS Widgets Core Gui REQUIRED)

# Incluir la carpeta src para que los #include funcionen
include_directories(src)

# --- BÚSQUEDA AUTOMÁTICA DE ARCHIVOS ---
# Esto busca CUALQUIER .cpp o .h dentro de 'src' y todas sus subcarpetas.
# Así no importa si mueves ScanController a 'core' o 'controller'.
file(GLOB_RECURSE PROJECT_SOURCES "src/*.cpp" "src/*.h")

# --- CREAR EJECUTABLE ---
add_executable(JtagScannerQt WIN32 ${PROJECT_SOURCES})

# --- LINKADO ---
target_link_libraries(JtagScannerQt PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui)

# Corrección para Linux (ignóralo en Windows)
if(UNIX AND NOT APPLE)
    target_link_libraries(JtagScannerQt PRIVATE dl)
endif()