Abrir la terminal en la carpeta en la que se almacene el "proyecto"

Ejecuta: cmake -S . -B build

Si eso no funciona. hay que añadir el path:

cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"

Entra en la carpeta build que se acaba de generar

Doble clic en JtagScannerQt.sln

Si no se tiene el CMAKE no hay problema, en breves intentaré actualizar el proyecto para que ya no sea necesario.
