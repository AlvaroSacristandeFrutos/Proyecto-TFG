cmake_minimum_required(VERSION 3.16)

# Nombre del proyecto
project(JtagScannerQt LANGUAGES CXX)

# Estándar C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configuración de build type por defecto
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
 
# Configuración Qt Automática
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON) 
set(CMAKE_AUTOUIC ON)

# Buscar Qt6
find_package(Qt6 COMPONENTS Widgets Core Gui SerialPort REQUIRED)

# Incluir la carpeta src para que los #include funcionen
include_directories(src)

# --- BÚSQUEDA AUTOMÁTICA DE ARCHIVOS ---
file(GLOB_RECURSE PROJECT_SOURCES "src/*.cpp" "src/*.h" "src/*.ui")

# --- CREAR EJECUTABLE ---
# TEMPORAL: Comentado WIN32 para ver logs de std::cout en consola
add_executable(JtagScannerQt ${PROJECT_SOURCES})

# --- CONFIGURACIÓN ESPECÍFICA PARA MSVC (Visual Studio) ---
if (MSVC)
    # /EHsc: Habilita el manejo de excepciones síncronas de C++ (Corrige C4530)
    # /utf-8: Interpreta los archivos fuente como UTF-8 (Corrige C4828)
    # /W3: Nivel de advertencia 3 (recomendado)
    target_compile_options(JtagScannerQt PRIVATE /EHsc /utf-8 /W3)
    
    # Evita advertencias sobre funciones 'inseguras' como sprintf
    target_compile_definitions(JtagScannerQt PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --- LINKADO ---
target_link_libraries(JtagScannerQt PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui Qt6::SerialPort)

# Corrección para Linux (por si compilas en otro SO en el futuro)
if(UNIX AND NOT APPLE)
    target_link_libraries(JtagScannerQt PRIVATE dl)
endif()