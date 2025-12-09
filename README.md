Instrucciones para compilar y ejecutar el proyecto en entorno Windows utilizando **Qt 6.7.3** y **MSVC 2022**.

## Requisitos Previos

* **CMake** (instalado y añadido al PATH).
* **Visual Studio 2022** (con las herramientas de C++).
* **Qt 6.7.3** para MSVC 2022 64-bit.

> **Nota sobre la ruta de Qt:**
> Los comandos a continuación asumen que Qt está instalado en la ruta por defecto: `C:\Qt\6.7.3\msvc2022_64`.
> Si tu instalación se encuentra en otra carpeta, por favor modifica la ruta en el comando de configuración.
## Pasos para Compilar (Modo Debug)

Abre una terminal (PowerShell o CMD) en la carpeta raíz del proyecto y ejecuta los siguientes comandos en orden:

### 1. Configurar el proyecto
Este comando genera la carpeta `build` y configura el proyecto vinculando las librerías de Qt. Dentro de ella estará la carpeta `Debug` en la que se encontrará el ejecutable con las librerias necesarias

```powershell
cmake -DCMAKE_PREFIX_PATH="C:\Qt\6.7.3\msvc2022_64" -S . -B build
cmake --build build --config Debug
C:\Qt\6.7.3\msvc2022_64\bin\windeployqt.exe build\Debug\JtagScannerQt.exe
```
```mermaid
classDiagram
    %% ============================================================
    %% CAPA GUI (Interfaz Gráfica)
    %% ============================================================
    namespace GUI {
        class MainWindow {
            -Ui::MainWindow* ui
            -unique_ptr~ScanController~ scanController
            -QThread* workerThread
            -ScanWorker* scanWorker
            -ChipVisualizer* chipVisualizer
            -QTimer* pollTimer
            -QGraphicsScene* waveformScene
            -map~string, int~ transitionCounters
            +MainWindow(parent)
            +~MainWindow()
            %% Slots de Menú
            -onNewProjectWizard()
            -onOpen()
            -onSave()
            -onJTAGConnection()
            -onRun()
            %% Slots de Pines
            -onSetTo0()
            -onSetTo1()
            -onTogglePinValue()
            %% Nuevos Slots Asíncronos
            +onPinsDataReady(vector~PinSnapshot~)
            +onScanError(QString)
            -setupBackend()
            -setupConnections()
        }
        class ChipVisualizer {
            -QGraphicsScene* m_scene
            -QMap~QString, PinGraphicsItem*~ m_pins
            +updatePinState(name, VisualPinState)
            +renderFromDeviceModel(DeviceModel)
            +createLayoutFromController(vector~string~)
            -addPin(name, number, x, y, side)
        }
        class PinGraphicsItem {
            -QString m_name
            -QString m_pinNumber
            -VisualPinState m_visualState
            +setState(VisualPinState)
            +setHighlighted(bool)
            +paint(painter, option, widget)
        }
        class VisualPinState {
            <<enumeration>>
            HIGH
            LOW
            OSCILLATING
            UNKNOWN
            LINKAGE
        }
    }
    %% ============================================================
    %% CAPA CONTROLLER & CONCURRENCIA
    %% ============================================================
    namespace Controller {
        class ScanController {
            <<QObject>>
            -unique_ptr~IJTAGAdapter~ adapter
            -unique_ptr~BoundaryScanEngine~ engine
            -unique_ptr~DeviceModel~ deviceModel
            -unique_ptr~BSDLCatalog~ bsdlCatalog
            -QThread* workerThread
            -ScanWorker* scanWorker
            -bool isDirty
            +ScanController()
            +connectAdapter(type, speed)
            +initialize()
            +detectAndLoad()
            +startPolling()
            +stopPolling()
            +setPinAsync(name, level)
            +getPinSnapshot() vector
            +applyChanges()
            +isInitialized() bool
            -- Signals --
            +pinsDataReady(vector)
            +errorOccurred(msg)
            -onPinsUpdated(vector)
            -onWorkerError(msg)
        }
        class ScanWorker {
            <<QObject>>
            -BoundaryScanEngine* engine
            -QTimer* pollTimer
            -mutex dirtyMutex
            -map~int, PinLevel~ dirtyPins
            -bool inEXTESTMode
            +ScanWorker(engine)
            +startPolling(interval)
            +stopPolling()
            +markDirtyPin(cellIndex, level)
            -run()
            -onPollTimer()
            -processDirtyPins()
            -switchToEXTEST()
            -switchToSAMPLE()
            -- Signals --
            +pinsUpdated(vector)
            +errorOccurred(msg)
        }
    }
    %% ============================================================
    %% CAPA CORE (Lógica de Negocio)
    %% ============================================================
    namespace Core {
        class BoundaryScanEngine {
            -IJTAGAdapter* adapter
            -vector~uint8_t~ bsr
            -vector~uint8_t~ bsr_buffer
            -size_t bsrLength
            -string currentInstruction
            +BoundaryScanEngine(adapter)
            +samplePins() bool
            +applyChanges() bool
            +readIDCODE() uint32_t
            +loadInstruction(opcode, len)
            +setPin(cellIndex, level)
            +getPin(cellIndex) PinLevel
            +setBSRLength(len)
        }
        class JtagStateMachine {
            +getPath(from, to) JtagPath
            +nextState(current, tms) TAPState
            -lookupTable
        }
        class TAPState {
            <<enumeration>>
            TEST_LOGIC_RESET
            RUN_TEST_IDLE
            SHIFT_DR
            SHIFT_IR
            UPDATE_DR
            etc...
        }
    }
    %% ============================================================
    %% CAPA BSDL & MODELO DE DATOS
    %% ============================================================
    namespace BSDL {
        class DeviceModel {
            -BSDLData data
            -map~string, PinInfo~ pins
            -map~string, uint32_t~ instructions
            +loadFromData(BSDLData)
            +getPinInfo(name) PinInfo
            +getOpcode(name) uint32_t
            +getBSRLength() size_t
            +getIDCODE() uint32_t
        }
        class BSDLCatalog {
            -map~uint32_t, string~ idcodeMap
            +scanDirectory(path)
            +findByIDCODE(idcode) string
            -extractIDCODE(path)
        }
        class BSDLParser {
            -BSDLData data
            +parse(filename) bool
            +getData() BSDLData
        }
        class BSDLData {
            <<struct>>
            +string entityName
            +uint32_t idCode
            +int boundaryLength
            +vector~BoundaryCell~ boundaryCells
            +vector~Instruction~ instructions
        }
        class BoundaryCell {
            <<struct>>
            +int cellNumber
            +string portName
            +CellFunction function
            +int controlCell
        }
        class PinInfo {
            <<struct>>
            +string name
            +int outputCell
            +int inputCell
            +int controlCell
        }
    }
    %% ============================================================
    %% CAPA HAL (Hardware Abstraction Layer)
    %% ============================================================
    namespace HAL {
        class AdapterFactory {
            +create(AdapterType) unique_ptr~IJTAGAdapter~
            +detectAdapters() vector~AdapterDescriptor~
        }
        class IJTAGAdapter {
            <<Interface>>
            +open() bool
            +close()
            +scanIR(instruction, len)* bool
            +scanDR(tdi, tdo, len)* bool
            +resetAndRun()* bool
            +shiftData(tdi, tdo, len) bool
            +writeTMS(sequence) bool
        }
        class JLinkAdapter {
            -DLL_HANDLE libHandle
            +scanIR(instruction, len)
            +scanDR(tdi, tdo, len)
            +resetAndRun()
            -loadLibrary()
            -getSymbol()
        }
        class PicoAdapter {
            -SerialTransport* serial
            +scanIR(instruction, len)
            +scanDR(tdi, tdo, len)
            +resetAndRun()
            -transceivePacket(cmd, payload)
        }
        class MockAdapter {
            +scanIR(...)
            +scanDR(...)
            +resetAndRun()
        }
        class JtagProtocol {
            <<enumeration>>
            CMD_SCAN_DR = 0x30
            CMD_SCAN_IR = 0x31
            CMD_RESET_TAP = 0x20
        }
    }
    %% ============================================================
    %% RELACIONES
    %% ============================================================
    %% GUI -> Controller
    MainWindow "1" *-- "1" ScanController : Posee
    MainWindow "1" o-- "1" ChipVisualizer : Usa
    ChipVisualizer "1" *-- "*" PinGraphicsItem : Contiene
    %% Controller -> Core & Worker
    ScanController "1" *-- "1" BoundaryScanEngine : Orquesta
    ScanController "1" *-- "1" DeviceModel : Consulta
    ScanController "1" *-- "1" BSDLCatalog : Usa
    ScanController "1" *-- "1" ScanWorker : Crea y mueve a Thread
    
    %% Worker -> Core
    ScanWorker "1" o-- "1" BoundaryScanEngine : Usa para polling
    %% Core -> HAL
    BoundaryScanEngine "1" o-- "1" IJTAGAdapter : Usa Polimórficamente
    BoundaryScanEngine ..> TAPState : Usa
    BoundaryScanEngine ..> JtagStateMachine : (Legacy/Helper)
    %% BSDL
    DeviceModel "1" *-- "1" BSDLData : Contiene
    DeviceModel ..> PinInfo : Retorna
    BSDLParser ..> BSDLData : Genera
    BSDLCatalog ..> BSDLParser : Usa internamente
    %% HAL Inheritance
    IJTAGAdapter <|-- JLinkAdapter
    IJTAGAdapter <|-- PicoAdapter
    IJTAGAdapter <|-- MockAdapter
    %% HAL Dependencies
    PicoAdapter ..> JtagProtocol : Usa Comandos
    AdapterFactory ..> IJTAGAdapter : Crea instacias
```
