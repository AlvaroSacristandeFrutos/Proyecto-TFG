#include "ChipVisualizer.h"
#include <QPainter>
#include <QBrush>
#include <QPen>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QToolTip>

// ============================================================================
// PinGraphicsItem Implementation
// ============================================================================

PinGraphicsItem::PinGraphicsItem(const QString& name, QGraphicsItem* parent)
    : QGraphicsRectItem(parent)
    , m_name(name)
    , m_label(new QGraphicsTextItem(name, this))
    , m_level(false)
    , m_enabled(false)
    , m_hovered(false)
{
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);

    // Position label
    QFont font = m_label->font();
    font.setPointSize(7);
    m_label->setFont(font);
    // Centrar etiqueta
    QRectF r = m_label->boundingRect();
    m_label->setPos(-r.width() / 2, -r.height() / 2);
}

void PinGraphicsItem::setPinState(bool level, bool enabled) {
    m_level = level;
    m_enabled = enabled;
    update();
}

void PinGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    QColor fillColor;
    if (!m_enabled) {
        fillColor = QColor(200, 200, 200); // Gray
    }
    else if (m_level) {
        fillColor = QColor(144, 238, 144); // Green
    }
    else {
        fillColor = QColor(100, 100, 100); // Dark Gray
    }

    if (m_hovered) {
        fillColor = fillColor.lighter(120);
    }

    painter->setBrush(QBrush(fillColor));
    painter->setPen(QPen(Qt::black, 1));
    painter->drawRect(rect());

    if (m_enabled) {
        painter->setPen(QPen(Qt::darkGreen, 2));
        painter->drawRect(rect().adjusted(1, 1, -1, -1));
    }
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

    createChipLayout();
}

ChipVisualizer::~ChipVisualizer() {
}

// --- ESTA ES LA FUNCIÓN QUE DABA PROBLEMAS ---
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
    int pinsPerSide = (totalPins + 3) / 4; // División redondeada hacia arriba

    for (int i = 0; i < totalPins; ++i) {
        QString name = QString::fromStdString(pins[i]);

        // Algoritmo de distribución simple
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
    m_pins.clear();
    m_scene->clear();
    createChipLayout();
}