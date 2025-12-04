Abrir la terminal en la carpeta en la que se almacene el "proyecto"

Ejecuta: cmake -S . -B build

Si eso no funciona. hay que añadir el path:

cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"

Entra en la carpeta build que se acaba de generar

Doble clic en JtagScannerQt.sln

Si no funciona, en el branch development está el .exe con todas las dll necesarias para ejecutar el estado actual (lo actualizaré con el código fuente a lo largo de la semana)
