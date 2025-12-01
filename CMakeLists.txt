cmake_minimum_required(VERSION 3.10)
project(BoundaryScannerImGui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Incluir src y src/imgui para encontrar los headers
include_directories(src)
include_directories(src/imgui)

# Buscar recursivamente tus fuentes y las de imgui
file(GLOB_RECURSE ALL_SOURCES "src/*.cpp" "src/*.h")

# Crear el ejecutable (WIN32 quita la consola negra de fondo, opcional)
add_executable(JtagGUI WIN32 ${ALL_SOURCES})

# LIBRERÍAS DE WINDOWS (DirectX 11)
# ImGui necesita esto para dibujar en pantalla
if(WIN32)
    target_link_libraries(JtagGUI d3d11 d3dcompiler)
endif()

# Si usas Linux en el futuro, necesitarías glfw/opengl, pero en Windows esto basta.