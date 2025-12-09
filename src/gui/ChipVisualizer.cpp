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
#include <cmath>

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
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    m_scene->setSceneRect(-300, -300, 600, 600);

    // NO crear pines dummy - esperar a que se cargue un BSDL
    // El usuario verá una escena vacía hasta conectar y cargar dispositivo
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
    // 1. Limpiar escena (primero clear() destruye items, luego clear() del map)
    m_scene->clear();
    m_pins.clear();
    m_chipBody = nullptr;  // Resetear puntero ya que clear() lo destruyó

    // 2. Obtener pines del modelo
    const auto& pins = model.getAllPins();
    if (pins.empty()) return;

    // NUEVO: Algoritmo para EDGE_PINS (TQFP/SOIC)
    if (m_layoutMode == LayoutMode::EDGE_PINS) {
        const double chipSize = 400.0;
        const double halfSize = chipSize / 2.0;

        // Dibujar cuerpo del chip
        m_chipBody = m_scene->addRect(-halfSize, -halfSize, chipSize, chipSize,
            QPen(Qt::black, 2), QBrush(QColor(50, 50, 50)));

        // Marca de orientación
        m_scene->addEllipse(-halfSize + 10, -halfSize + 10, 20, 20,
            QPen(Qt::white, 2), QBrush(Qt::white));

        int totalPins = pins.size();
        int pinsPerSide = (totalPins + 3) / 4;
        const double margin = 50.0;

        for (size_t i = 0; i < pins.size(); i++) {
            int side = i / pinsPerSide;
            int posInSide = i % pinsPerSide;
            double spacing = (chipSize - 2 * margin) / (pinsPerSide > 1 ? pinsPerSide - 1 : 1);

            double x, y;
            PinSide pinSide;

            switch (side) {
                case 0: // LEFT
                    x = -halfSize;
                    y = -halfSize + margin + posInSide * spacing;
                    pinSide = PinSide::LEFT;
                    break;
                case 1: // TOP
                    x = -halfSize + margin + posInSide * spacing;
                    y = -halfSize;
                    pinSide = PinSide::TOP;
                    break;
                case 2: // RIGHT
                    x = halfSize;
                    y = -halfSize + margin + posInSide * spacing;
                    pinSide = PinSide::RIGHT;
                    break;
                case 3: // BOTTOM
                default:
                    x = -halfSize + margin + posInSide * spacing;
                    y = halfSize;
                    pinSide = PinSide::BOTTOM;
                    break;
            }

            addPin(QString::fromStdString(pins[i].name),
                   QString::fromStdString(pins[i].pinNumber),
                   x, y, pinSide);
        }
        return;
    }

    // MANTENER: Algoritmo actual de BGA (center grid)
    // 3. Calcular cuadrícula óptima para distribución uniforme
    int totalPins = pins.size();

    // Calcular número de columnas y filas para una distribución cuadrada
    int gridCols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(totalPins))));
    int gridRows = static_cast<int>(std::ceil(static_cast<double>(totalPins) / gridCols));

    const double chipSize = 400.0;
    const double halfSize = chipSize / 2.0;
    const double margin = 20.0;  // Margen desde el borde

    // Calcular espaciado uniforme entre pines
    double availableWidth = chipSize - (2 * margin);
    double availableHeight = chipSize - (2 * margin);
    double spacingX = (gridCols > 1) ? availableWidth / (gridCols - 1) : availableWidth;
    double spacingY = (gridRows > 1) ? availableHeight / (gridRows - 1) : availableHeight;

    // Calcular tamaño del pin adaptativo (60-80% del espaciado mínimo)
    double minSpacing = std::min(spacingX, spacingY);
    double pinSize = std::max(4.0, std::min(15.0, minSpacing * 0.7));  // Entre 4px y 15px

    // 4. Dibujar cuerpo del chip
    m_chipBody = m_scene->addRect(-halfSize, -halfSize, chipSize, chipSize,
        QPen(Qt::black, 2), QBrush(QColor(50, 50, 50)));

    // Marca de orientación (esquina superior izquierda)
    m_scene->addEllipse(-halfSize + 10, -halfSize + 10, 20, 20,
        QPen(Qt::white, 2), QBrush(Qt::white));

    // 5. Renderizar pines en matriz uniforme
    int pinIndex = 0;
    for (const auto& pin : pins) {
        if (pin.pinNumber.empty()) continue;

        // Calcular posición en la cuadrícula (distribución uniforme)
        int row = pinIndex / gridCols;
        int col = pinIndex % gridCols;

        // Calcular posición en píxeles
        double x = -halfSize + margin + (col * spacingX) - (pinSize / 2);
        double y = -halfSize + margin + (row * spacingY) - (pinSize / 2);

        QString name = QString::fromStdString(pin.name);
        QString pinNumber = QString::fromStdString(pin.pinNumber);

        // Determinar el lado para la orientación del label
        PinSide side = determineSide(row, col, gridRows - 1, gridCols - 1);

        addPin(name, pinNumber, x, y, side, pinSize);
        pinIndex++;
    }

    // 7. Agregar leyenda de colores
    const double legendX = halfSize + 30;  // A la derecha del chip
    const double legendY = -halfSize;
    const double legendBoxSize = 15;
    const double legendSpacing = 25;

    QFont legendFont;
    legendFont.setPointSize(9);
    legendFont.setBold(true);

    // Título de la leyenda
    QGraphicsTextItem* legendTitle = m_scene->addText("Pin States:", legendFont);
    legendTitle->setPos(legendX, legendY - 30);
    legendTitle->setDefaultTextColor(Qt::black);

    // Array de estados y sus descripciones
    struct LegendEntry {
        VisualPinState state;
        QString label;
    };

    LegendEntry entries[] = {
        {VisualPinState::HIGH, "HIGH - Logic level 1"},
        {VisualPinState::LOW, "LOW - Logic level 0"},
        {VisualPinState::OSCILLATING, "OSCILLATING - Rapid changes"},
        {VisualPinState::UNKNOWN, "UNKNOWN - Not sampled"},
        {VisualPinState::LINKAGE, "LINKAGE - Not controllable"}
    };

    // Función auxiliar para obtener el color
    auto getColorForState = [](VisualPinState state) -> QColor {
        switch (state) {
        case VisualPinState::HIGH: return QColor(220, 50, 50);
        case VisualPinState::LOW: return QColor(50, 100, 220);
        case VisualPinState::OSCILLATING: return QColor(255, 200, 0);
        case VisualPinState::UNKNOWN: return QColor(150, 150, 150);
        case VisualPinState::LINKAGE: return QColor(40, 40, 40);
        default: return QColor(150, 150, 150);
        }
    };

    // Dibujar cada entrada de la leyenda
    for (int i = 0; i < 5; ++i) {
        double y = legendY + (i * legendSpacing);

        // Cuadrado de color
        QGraphicsRectItem* colorBox = m_scene->addRect(
            legendX, y, legendBoxSize, legendBoxSize,
            QPen(Qt::black, 1),
            QBrush(getColorForState(entries[i].state))
        );

        // Texto descriptivo
        QGraphicsTextItem* text = m_scene->addText(entries[i].label, legendFont);
        text->setPos(legendX + legendBoxSize + 5, y - 3);
        text->setDefaultTextColor(Qt::black);
    }

    // Ajustar vista automáticamente a TODO el contenido (incluyendo leyenda)
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