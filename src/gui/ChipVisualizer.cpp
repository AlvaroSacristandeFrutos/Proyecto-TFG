#include "ChipVisualizer.h"
#include <QPainter>
#include <QBrush>
#include <QPen>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QToolTip>

// NUEVO: Para renderFromDeviceModel()
#include "../bsdl/DeviceModel.h"
#include <algorithm>
#include <vector>
#include <cmath>
#include <string>
#include <cctype>
#include <cstdio> // Para snprintf
#include <type_traits>

// ============================================================================
// PinGraphicsItem Implementation
// ============================================================================

PinGraphicsItem::PinGraphicsItem(const QString& pinNumber, QGraphicsItem* parent)
    : QGraphicsRectItem(parent)
    , m_name("")  // Will be set later via setPinName()
    , m_pinNumber(pinNumber)
    , m_label(new QGraphicsTextItem(pinNumber, this))
    , m_level(false)
    , m_enabled(false)
    , m_hovered(false)
    , m_visualState(VisualPinState::UNKNOWN)
    , m_highlighted(false)
{
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsSelectable, false);  // Selection via table

    // Ocultar label - ahora usaremos una leyenda externa
    m_label->setVisible(false);
}

void PinGraphicsItem::setPinState(bool level, bool enabled) {
    m_level = level;
    m_enabled = enabled;
    update();
}

void PinGraphicsItem::setLabelRotation(double angle) {
    // Labels SIEMPRE horizontales, solo cambia la POSICIÓN según el lado
    // angle indica el lado: 0=TOP, 90=LEFT, 180=BOTTOM, -90=RIGHT

    QRectF r = m_label->boundingRect();
    m_label->setRotation(0);  // SIEMPRE horizontal

    // Posicionar según el lado
    if (angle == 90) {
        // LEFT: label a la IZQUIERDA del pin
        m_label->setPos(-r.width() - 2, -r.height() / 2);
    } else if (angle == -90 || angle == 270) {
        // RIGHT: label a la DERECHA del pin
        m_label->setPos(10, -r.height() / 2);  // 10px = 8px pin + 2px gap
    } else if (angle == 180) {
        // BOTTOM: label DEBAJO del pin
        m_label->setPos(-r.width() / 2, 10);
    } else {
        // TOP (0): label ARRIBA del pin
        m_label->setPos(-r.width() / 2, -r.height() - 2);
    }
}

void PinGraphicsItem::setPinName(const QString& name) {
    m_name = name;
    // Don't change label - it shows pinNumber, not name
}

void PinGraphicsItem::setPinNumber(const QString& number) {
    m_pinNumber = number;
    m_label->setPlainText(number);
    // Label position will be set by setLabelRotation()
}

void PinGraphicsItem::setState(VisualPinState state) {
    m_visualState = state;
    update();  // Trigger repaint
}

void PinGraphicsItem::setHighlighted(bool highlighted) {
    m_highlighted = highlighted;
    update();  // Trigger repaint
}

QColor PinGraphicsItem::getColorForState(VisualPinState state) const {
    switch (state) {
    case VisualPinState::HIGH:
        return QColor(220, 50, 50);      // Rojo
    case VisualPinState::LOW:
        return QColor(50, 100, 220);     // Azul
    case VisualPinState::OSCILLATING:
        return QColor(255, 200, 0);      // Amarillo
    case VisualPinState::UNKNOWN:
        return QColor(150, 150, 150);    // Gris
    case VisualPinState::LINKAGE:
        return QColor(40, 40, 40);       // Negro
    default:
        return QColor(150, 150, 150);    // Gris por defecto
    }
}

void PinGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    // Color de relleno según estado visual
    QColor fillColor = getColorForState(m_visualState);

    // Hover effect: lighter
    if (m_hovered) {
        fillColor = fillColor.lighter(120);
    }

    painter->setBrush(QBrush(fillColor));

    // Borde: naranja si seleccionado, negro si no
    QPen pen;
    if (m_highlighted) {
        pen.setColor(QColor(255, 128, 0));  // Naranja
        pen.setWidth(3);
    } else {
        pen.setColor(Qt::black);
        pen.setWidth(1);
    }
    painter->setPen(pen);

    // Dibujar rectángulo del pin
    painter->drawRect(rect());
}

void PinGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        setSelected(true);
    }
    QGraphicsRectItem::mousePressEvent(event);
}

void PinGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    Q_UNUSED(event);
    m_hovered = true;
    update();
    QString tooltip = QString("%1: %2").arg(m_name).arg(m_level ? "HIGH" : "LOW");
    setToolTip(tooltip);
}

void PinGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    Q_UNUSED(event);
    m_hovered = false;
    update();
}

// ============================================================================
// ChipVisualizer Implementation
// ============================================================================

ChipVisualizer::ChipVisualizer(QWidget* parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
    , m_chipBody(nullptr)
    , m_packageType("LQFP100")
{
    m_chipWidth = 400.0;
    m_chipHeight = 400.0;

    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    m_scene->setSceneRect(-300, -300, 600, 600);

    // NO crear pines dummy - esperar a que se cargue un BSDL
    // El usuario verá una escena vacía hasta conectar y cargar dispositivo
}

void ChipVisualizer::setChipSize(double width, double height) {
    // Protección básica para evitar tamaños negativos o cero
    if (width <= 50.0) width = 50.0;
    if (height <= 50.0) height = 50.0;

    m_chipWidth = width;
    m_chipHeight = height;
}

ChipVisualizer::~ChipVisualizer() {
}

// --- ESTA ES LA FUNCI�N QUE DABA PROBLEMAS ---
void ChipVisualizer::createLayoutFromController(const std::vector<std::string>& pins) {
    // Limpiar escena antigua
    m_pins.clear();
    m_scene->clear();

    // Redibujar cuerpo del chip
    m_chipBody = m_scene->addRect(-CHIP_WIDTH / 2, -CHIP_HEIGHT / 2, CHIP_WIDTH, CHIP_HEIGHT,
        QPen(Qt::black), QBrush(QColor(50, 50, 50)));

    int totalPins = (int)pins.size();
    if (totalPins == 0) return;

    // Distribuir pines en los 4 lados
    int pinsPerSide = (totalPins + 3) / 4; // Divisi�n redondeada hacia arriba

    for (int i = 0; i < totalPins; ++i) {
        QString name = QString::fromStdString(pins[i]);

        // Algoritmo de distribuci�n simple
        if (i < pinsPerSide)
            addPin(name, i, LEFT);
        else if (i < pinsPerSide * 2)
            addPin(name, i - pinsPerSide, BOTTOM);
        else if (i < pinsPerSide * 3)
            addPin(name, i - pinsPerSide * 2, RIGHT);
        else
            addPin(name, i - pinsPerSide * 3, TOP);
    }
}

void ChipVisualizer::createChipLayout() {
    // Si no hay pines cargados, mostrar un dummy
    if (m_pins.isEmpty()) {
        createLQFP100Layout();
    }
}

void ChipVisualizer::createLQFP100Layout() {
    m_chipBody = m_scene->addRect(-CHIP_WIDTH / 2, -CHIP_HEIGHT / 2, CHIP_WIDTH, CHIP_HEIGHT,
        QPen(Qt::black), QBrush(QColor(50, 50, 50)));

    // Pines de ejemplo si no hay BSDL cargado
    addPin("PA0", 0, LEFT);
    addPin("PA1", 1, LEFT);
    addPin("RESET", 0, TOP);
    addPin("GND", 0, BOTTOM);
}

void ChipVisualizer::addPin(const QString& name, int position, PinSide side) {
    PinGraphicsItem* pin = new PinGraphicsItem(name);
    qreal x = 0, y = 0;

    switch (side) {
    case LEFT:
        x = -CHIP_WIDTH / 2 - PIN_WIDTH;
        y = -CHIP_HEIGHT / 2 + position * PIN_SPACING + 20;
        pin->setRect(0, 0, PIN_WIDTH, PIN_HEIGHT);
        break;
    case RIGHT:
        x = CHIP_WIDTH / 2;
        y = -CHIP_HEIGHT / 2 + position * PIN_SPACING + 20;
        pin->setRect(0, 0, PIN_WIDTH, PIN_HEIGHT);
        break;
    case TOP:
        x = -CHIP_WIDTH / 2 + position * PIN_SPACING + 20;
        y = -CHIP_HEIGHT / 2 - PIN_WIDTH;
        pin->setRect(0, 0, PIN_HEIGHT, PIN_WIDTH);
        break;
    case BOTTOM:
        x = -CHIP_WIDTH / 2 + position * PIN_SPACING + 20;
        y = CHIP_HEIGHT / 2;
        pin->setRect(0, 0, PIN_HEIGHT, PIN_WIDTH);
        break;
    }

    pin->setPos(x, y);
    m_scene->addItem(pin);
    m_pins[name] = pin;
}

void ChipVisualizer::updatePinStates(const QList<PinState>& states) {
    for (const auto& state : states) {
        if (m_pins.contains(state.name)) {
            m_pins[state.name]->setPinState(state.level, state.enabled);
        }
    }
    m_scene->update();
}

void ChipVisualizer::clearPins() {
    for (auto* pin : m_pins) {
        pin->setPinState(false, false);
    }
    m_scene->update();
}

void ChipVisualizer::setPackageType(const QString& packageType) {
    m_packageType = packageType;

    if (packageType == "EDGE" || packageType.contains("TQFP") ||
        packageType.contains("SOIC") || packageType.contains("QFP")) {
        m_layoutMode = LayoutMode::EDGE_PINS;
    } else {
        m_layoutMode = LayoutMode::CENTER_GRID;
    }

    m_pins.clear();
    m_scene->clear();
    createChipLayout();
}

void ChipVisualizer::updatePinName(const QString& oldName, const QString& newName) {
    // Buscar el pin con el nombre antiguo
    if (m_pins.contains(oldName)) {
        PinGraphicsItem* pin = m_pins[oldName];

        // Actualizar el mapa (remover entrada vieja, agregar nueva)
        m_pins.remove(oldName);
        m_pins[newName] = pin;

        // Actualizar el nombre y label visual del pin
        pin->setPinName(newName);

        // Actualizar el tooltip
        QString currentTooltip = pin->toolTip();
        QString number = currentTooltip.section('(', 1, 1).section(')', 0, 0);
        pin->setToolTip(QString("%1 (%2)").arg(newName).arg(number));
    }
}

// ============================================================================
// NUEVO: Layout Real desde DeviceModel
// ============================================================================

ChipVisualizer::ParsedPin ChipVisualizer::parsePinNumber(const QString& pinNumber) {
    ParsedPin result{0, 0, false};
    if (pinNumber.isEmpty()) return result;

    // Separar letras (row) y números (col)
    int i = 0;
    int rowVal = 0;

    // Parsear letras: A=0, B=1, ..., Z=25, AA=26, AB=27
    while (i < pinNumber.size() && pinNumber[i].isLetter()) {
        rowVal = rowVal * 26 + (pinNumber[i].toUpper().toLatin1() - 'A');
        i++;
    }

    // Parsear números: "7" → 6 (0-based)
    QString colStr = pinNumber.mid(i);
    if (colStr.isEmpty()) return result;

    result.row = rowVal;
    result.col = colStr.toInt() - 1;
    result.valid = true;
    return result;
}

ChipVisualizer::PinSide ChipVisualizer::determineSide(int row, int col, int maxRow, int maxCol) {
    // Heurística simple: dividir el chip en 4 cuadrantes
    // Si está en un borde → ese lado
    // Si no → lado más cercano

    if (row == 0) return TOP;
    if (row == maxRow) return BOTTOM;
    if (col == 0) return LEFT;
    if (col == maxCol) return RIGHT;

    // Cuadrantes internos
    double rowRatio = (double)row / maxRow;
    double colRatio = (double)col / maxCol;

    if (rowRatio < colRatio && rowRatio < (1 - colRatio)) return TOP;
    if (rowRatio > colRatio && rowRatio > (1 - colRatio)) return BOTTOM;
    if (colRatio < rowRatio && colRatio < (1 - rowRatio)) return LEFT;
    return RIGHT;
}

void ChipVisualizer::addPin(const QString& name, const QString& number, double x, double y, PinSide side, double pinSize) {
    // Crear pin gráfico con NÚMERO visible (no nombre)
    PinGraphicsItem* pin = new PinGraphicsItem(number);  // ← NÚMERO aquí
    pin->setRect(0, 0, pinSize, pinSize);  // Tamaño adaptativo
    pin->setPos(x, y);

    // Set logical name (for lookups)
    pin->setPinName(name);

    // Estado inicial: UNKNOWN (gris)
    pin->setState(VisualPinState::UNKNOWN);

    // Posicionar label según el lado (SIEMPRE horizontal, solo cambia posición)
    switch (side) {
    case LEFT:
        pin->setLabelRotation(90);  // 90 = posición a la IZQUIERDA
        break;
    case RIGHT:
        pin->setLabelRotation(-90);  // -90 = posición a la DERECHA
        break;
    case TOP:
        pin->setLabelRotation(0);  // 0 = posición ARRIBA
        break;
    case BOTTOM:
        pin->setLabelRotation(180);  // 180 = posición ABAJO
        break;
    }

    // Tooltip con nombre + pin físico: "PA0 (A1)"
    pin->setToolTip(QString("%1 (%2)").arg(name).arg(number));

    m_scene->addItem(pin);
    m_pins[name] = pin;  // Key por NOMBRE (para buscar desde tabla)
}

void ChipVisualizer::renderFromDeviceModel(const JTAG::DeviceModel& model) {
    // 1. Limpieza obligatoria de la escena (Qt)
    m_scene->clear();
    m_pins.clear();
    m_chipBody = nullptr;

    // 2. Obtener pines
    auto pins = model.getAllPins();
    if (pins.empty()) return;

    // Detectar tipo de pin automáticamente
    using PinType = typename std::decay<decltype(pins[0])>::type;

    // -----------------------------------------------------------------------
    // LÓGICA DE ORDENACIÓN (C++ PURO - SIN QT)
    // -----------------------------------------------------------------------
    // Ordena: P1, P2, P3... P10 (numérico) en lugar de P1, P10, P2 (alfabético)
    std::sort(pins.begin(), pins.end(), [](const PinType& a, const PinType& b) {
        std::string sA = a.pinNumber;
        std::string sB = b.pinNumber;

        // Función lambda local para extraer números de std::string
        auto extractNumber = [](const std::string& s) -> int {
            std::string numStr;
            for (char c : s) {
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    numStr += c;
                }
            }
            return numStr.empty() ? 0 : std::stoi(numStr);
            };

        int nA = extractNumber(sA);
        int nB = extractNumber(sB);

        // Prioridad: Orden numérico si existen números distintos
        if (nA != 0 && nB != 0 && nA != nB) {
            return nA < nB;
        }
        // Fallback: Orden alfabético
        return sA < sB;
        });

    // -----------------------------------------------------------------------
    // PREPARACIÓN DE DATOS
    // -----------------------------------------------------------------------

    // IDCODE: Formato C estándar para evitar dependencias de Qt aquí
    uint32_t actualIdCode = 0x12345678; // TODO: Usar model.idCode si está disponible
    char idBuffer[64];
    std::snprintf(idBuffer, sizeof(idBuffer), "IDCODE: 0x%08X", actualIdCode);
    QString idCodeText = QString::fromLatin1(idBuffer);

    int totalPins = static_cast<int>(pins.size());

    // --- TAMAÑO DEL CHIP ---
    // NOTA: He puesto esto como variables locales para solucionar tu error de compilación.
    // Si quieres usar valores del Wizard, asegúrate de pasarlos a esta clase.
    double w = this->m_chipWidth;
    double h = this->m_chipHeight;

    // TODO: Si en el futuro añades las variables al .h, descomenta esto:
    // w = this->m_chipWidth;
    // h = this->m_chipHeight;
    // if (w <= 50.0) w = 400.0; // Protección
    // if (h <= 50.0) h = 400.0;

    double hw = w / 2.0;
    double hh = h / 2.0;

    // --- DISTRIBUCIÓN DE PINES ---
    // Calculamos cuántos pines van en cada lado para distribución equitativa
    int nTop = 0, nRight = 0, nBottom = 0, nLeft = 0;

    nTop = (totalPins + 3) / 4;
    nRight = (totalPins > nTop) ? (totalPins - nTop + 2) / 3 : 0;
    nBottom = (totalPins > nTop + nRight) ? (totalPins - nTop - nRight + 1) / 2 : 0;
    nLeft = totalPins - nTop - nRight - nBottom;

    // -----------------------------------------------------------------------
    // RENDERIZADO (DIBUJO EN SCENE)
    // -----------------------------------------------------------------------

    if (m_layoutMode == LayoutMode::EDGE_PINS) {

        // Cuerpo y Marca
        m_chipBody = m_scene->addRect(-hw, -hh, w, h, QPen(Qt::black, 2), QBrush(Qt::white));
        m_scene->addEllipse(-hw + 8, -hh + 8, 15, 15, QPen(Qt::black, 2), QBrush(Qt::white));

        // Texto IDCODE
        QFont idFont; idFont.setPointSize(14); idFont.setBold(true);
        QGraphicsTextItem* idItem = m_scene->addText(idCodeText, idFont);
        QRectF r = idItem->boundingRect();
        idItem->setPos(-r.width() / 2.0, -r.height() / 2.0); // Centrar

        // --- BUCLE DE COLOCACIÓN (Orden: Top -> Right -> Bottom -> Left) ---
        int pIdx = 0;
        const double margin = 40.0;

        // 1. SUPERIOR (Top): De Izquierda a Derecha -> Pin 1 empieza aquí
        if (nTop > 0) {
            double available = w - (2 * margin);
            double spacing = (nTop > 1) ? available / (nTop - 1) : available;
            // Tamaño adaptativo (entre 4px y 18px)
            double pinSize = (spacing * 0.8 > 18.0) ? 18.0 : ((spacing * 0.8 < 4.0) ? 4.0 : spacing * 0.8);

            for (int i = 0; i < nTop && pIdx < totalPins; ++i) {
                double x = -hw + margin + (i * spacing) - (pinSize / 2.0);
                // Y = Borde superior (-hh) menos tamaño del pin (hacia fuera)
                addPin(QString::fromStdString(pins[pIdx].name),
                    QString::fromStdString(pins[pIdx].pinNumber),
                    x, -hh - pinSize, PinSide::TOP, pinSize);
                pIdx++;
            }
        }

        // 2. DERECHO (Right): De Arriba hacia Abajo
        if (nRight > 0) {
            double available = h - (2 * margin);
            double spacing = (nRight > 1) ? available / (nRight - 1) : available;
            double pinSize = (spacing * 0.8 > 18.0) ? 18.0 : ((spacing * 0.8 < 4.0) ? 4.0 : spacing * 0.8);

            for (int i = 0; i < nRight && pIdx < totalPins; ++i) {
                double y = -hh + margin + (i * spacing) - (pinSize / 2.0);
                // X = Borde derecho (+hw)
                addPin(QString::fromStdString(pins[pIdx].name),
                    QString::fromStdString(pins[pIdx].pinNumber),
                    hw, y, PinSide::RIGHT, pinSize);
                pIdx++;
            }
        }

        // 3. INFERIOR (Bottom): De Derecha a Izquierda
        if (nBottom > 0) {
            double available = w - (2 * margin);
            double spacing = (nBottom > 1) ? available / (nBottom - 1) : available;
            double pinSize = (spacing * 0.8 > 18.0) ? 18.0 : ((spacing * 0.8 < 4.0) ? 4.0 : spacing * 0.8);

            for (int i = 0; i < nBottom && pIdx < totalPins; ++i) {
                // Avanzamos de derecha a izquierda
                double x = hw - margin - (i * spacing) - (pinSize / 2.0);
                // Y = Borde inferior (+hh)
                addPin(QString::fromStdString(pins[pIdx].name),
                    QString::fromStdString(pins[pIdx].pinNumber),
                    x, hh, PinSide::BOTTOM, pinSize);
                pIdx++;
            }
        }

        // 4. IZQUIERDO (Left): De Abajo hacia Arriba
        if (nLeft > 0) {
            double available = h - (2 * margin);
            double spacing = (nLeft > 1) ? available / (nLeft - 1) : available;
            double pinSize = (spacing * 0.8 > 18.0) ? 18.0 : ((spacing * 0.8 < 4.0) ? 4.0 : spacing * 0.8);

            for (int i = 0; i < nLeft && pIdx < totalPins; ++i) {
                // Avanzamos de abajo hacia arriba
                double y = hh - margin - (i * spacing) - (pinSize / 2.0);
                // X = Borde izquierdo (-hw) menos tamaño del pin (hacia fuera)
                addPin(QString::fromStdString(pins[pIdx].name),
                    QString::fromStdString(pins[pIdx].pinNumber),
                    -hw - pinSize, y, PinSide::LEFT, pinSize);
                pIdx++;
            }
        }

    }
    else {
        // --- MODO BGA / CENTER GRID ---

        // 1. Cálculo inteligente de filas/cols basado en el Aspect Ratio (Ancho/Alto)
        // Esto "adapta la altura al tamaño" distribuyendo los pines mejor.
        double aspectRatio = (h > 0) ? w / h : 1.0;

        // Fórmula: cols * rows = totalPins  Y  cols / rows = aspectRatio
        // rows^2 = totalPins / aspectRatio
        int rows = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(totalPins) / aspectRatio)));
        if (rows < 1) rows = 1;
        int cols = static_cast<int>(std::ceil(static_cast<double>(totalPins) / rows));

        // Ajuste fino por si el redondeo dejó huecos o sobrantes
        while (rows * cols < totalPins) cols++;

        // 2. Cálculos de espaciado
        // Márgenes internos para que no toque el borde exacto
        double padding = 40.0;
        double effectiveW = w - padding;
        double effectiveH = h - padding;

        double spX = effectiveW / ((cols > 1) ? cols - 1 : 1);
        double spY = effectiveH / ((rows > 1) ? rows - 1 : 1);

        // Elegir el tamaño de pin basado en el espacio más pequeño disponible
        double minSp = (spX < spY) ? spX : spY;
        // Tamaño dinámico: entre 4px y 25px
        double size = (minSp * 0.8 > 25.0) ? 25.0 : ((minSp * 0.8 < 4.0) ? 4.0 : minSp * 0.8);

        // 3. Dibujar Cuerpo (FONDO BLANCO SOLICITADO)
        // Usamos Qt::white en lugar de QColor(50,50,50)
        m_chipBody = m_scene->addRect(-hw, -hh, w, h, QPen(Qt::black, 3), QBrush(Qt::white));

        // Marca de Pin 1 (Círculo negro en esquina superior izquierda)
        m_scene->addEllipse(-hw + 8, -hh + 8, 15, 15, QPen(Qt::black, 2), QBrush(Qt::black));

        // 4. Bucle de colocación (Izquierda -> Derecha, Arriba -> Abajo)
        int idx = 0;
        for (const auto& p : pins) {
            // Calcular fila y columna actual
            int r = idx / cols;
            int c = idx % cols;

            // Coordenadas: Inicio (-hw) + Margen + (Columna * Espacio) - Centro del Pin
            double x = -hw + (padding / 2.0) + (c * spX) - (size / 2.0);
            double y = -hh + (padding / 2.0) + (r * spY) - (size / 2.0);

            // Side: Determinamos el lado solo para la orientación de la etiqueta (opcional en BGA)
            // Usamos una lógica simple: primera mitad = TOP, segunda = BOTTOM para etiquetas
            PinSide labelSide = (r < rows / 2) ? TOP : BOTTOM;

            addPin(QString::fromStdString(p.name),
                QString::fromStdString(p.pinNumber),
                x, y, labelSide, size);
            idx++;
        }
    }

    // --- LEYENDA (Usando Qt para pintar) ---
    double lx = hw + 60.0; double ly = -hh;
    QFont lf; lf.setPointSize(9); lf.setBold(true);
    m_scene->addText("Pin States:", lf)->setPos(lx, ly - 30.0);

    struct LegendItem { VisualPinState s; const char* text; };
    LegendItem items[] = {
        {VisualPinState::HIGH, "HIGH - Logic level 1"},
        {VisualPinState::LOW, "LOW - Logic level 0"},
        {VisualPinState::OSCILLATING, "OSCILLATING - Rapid changes"},
        {VisualPinState::UNKNOWN, "UNKNOWN - Not sampled"},
        {VisualPinState::LINKAGE, "LINKAGE - Not controllable"}
    };

    auto getColor = [](VisualPinState s) -> QColor {
        switch (s) {
        case VisualPinState::HIGH: return QColor(220, 50, 50);
        case VisualPinState::LOW: return QColor(50, 100, 220);
        case VisualPinState::OSCILLATING: return QColor(255, 200, 0);
        case VisualPinState::UNKNOWN: return QColor(150, 150, 150);
        default: return QColor(40, 40, 40);
        }
        };

    for (int i = 0; i < 5; ++i) {
        double y = ly + (i * 25.0);
        m_scene->addRect(lx, y, 15, 15, QPen(Qt::black, 1), QBrush(getColor(items[i].s)));
        m_scene->addText(items[i].text, lf)->setPos(lx + 20.0, y - 3.0);
    }

    setSceneRect(m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20));
    fitInView(sceneRect(), Qt::KeepAspectRatio);
}
// ==================================================================
// NEW: Color-Coded Visualization Methods
// ==================================================================

void ChipVisualizer::updatePinState(const QString& pinName, VisualPinState state) {
    // m_pins usa pinName como key
    if (m_pins.contains(pinName)) {
        m_pins[pinName]->setState(state);
    }
}

void ChipVisualizer::highlightPin(const QString& pinName) {
    // Limpiar selección anterior
    clearHighlight();

    // Resaltar nuevo pin
    if (m_pins.contains(pinName)) {
        m_pins[pinName]->setHighlighted(true);
    }
}

void ChipVisualizer::clearHighlight() {
    for (auto* pin : m_pins.values()) {
        pin->setHighlighted(false);
    }
}
void ChipVisualizer::setCustomDimensions(double width, double height) {
    // En lugar de inventar variables nuevas, usamos el setter que ya creaste
    // o asignamos directamente a las variables de la clase.
    setChipSize(width, height);
}

void ChipVisualizer::renderPlaceholder(uint32_t idcode) {
    m_scene->clear();
    m_pins.clear();
    m_chipBody = nullptr;

    // ERROR CORREGIDO AQUI:
    // Usamos m_chipWidth en lugar de m_customWidth
    double w = m_chipWidth;
    double h = m_chipHeight;

    // Dibujar cuerpo base usando las dimensiones personalizadas
    m_chipBody = m_scene->addRect(-w / 2, -h / 2, w, h,
        QPen(Qt::black, 3), QBrush(QColor(60, 60, 70)));

    // Marca de pin 1 (esquina superior izquierda)
    double markSize = std::min(w, h) * 0.05;
    m_scene->addEllipse(-w / 2 + markSize, -h / 2 + markSize, markSize, markSize,
        QPen(Qt::white), QBrush(Qt::white));

    // Mostrar IDCODE en el centro
    QFont font("Arial", 12, QFont::Bold);
    QString text = QString("IDCODE\n0x%1").arg(idcode, 8, 16, QChar('0')).toUpper();

    QGraphicsTextItem* label = m_scene->addText(text, font);
    label->setDefaultTextColor(Qt::white);

    // Centrar texto
    QRectF textRect = label->boundingRect();
    label->setPos(-textRect.width() / 2, -textRect.height() / 2);

    // Ajustar vista
    setSceneRect(m_scene->itemsBoundingRect().adjusted(-50, -50, 50, 50));
    fitInView(sceneRect(), Qt::KeepAspectRatio);
}