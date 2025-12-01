#ifndef CHIPVISUALIZER_H
#define CHIPVISUALIZER_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QMap>
#include <QString>
#include <vector> // Necesario para std::vector
#include <string> // Necesario para std::string

/**
 * @brief Pin state structure for visualization
 */
struct PinState {
    QString name;
    bool level;      // true = HIGH, false = LOW
    bool enabled;    // true = can be controlled (EXTEST mode)
};

/**
 * @brief Graphical pin item with color-coded state
 */
class PinGraphicsItem : public QGraphicsRectItem {
public:
    PinGraphicsItem(const QString& name, QGraphicsItem* parent = nullptr);

    void setPinState(bool level, bool enabled);
    QString getName() const { return m_name; }

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    QString m_name;
    QGraphicsTextItem* m_label;
    bool m_level;
    bool m_enabled;
    bool m_hovered;
};

/**
 * @brief Chip visualization widget (TopJTAG style)
 */
class ChipVisualizer : public QGraphicsView {
    Q_OBJECT

public:
    explicit ChipVisualizer(QWidget* parent = nullptr);
    ~ChipVisualizer() override;

    void updatePinStates(const QList<PinState>& states);
    void clearPins();
    void setPackageType(const QString& packageType);

    // --- ESTA ERA LA FUNCIÓN QUE FALTABA DECLARAR ---
    void createLayoutFromController(const std::vector<std::string>& pins);

signals:
    void pinClicked(const QString& pinName);

private:
    // Enum definido ANTES de usarse
    enum PinSide {
        LEFT,
        RIGHT,
        TOP,
        BOTTOM
    };

    void createChipLayout();
    void createLQFP100Layout();
    void addPin(const QString& name, int position, PinSide side);

    QGraphicsScene* m_scene;
    QGraphicsRectItem* m_chipBody;
    QMap<QString, PinGraphicsItem*> m_pins;
    QString m_packageType;

    // Layout constants
    static constexpr int CHIP_WIDTH = 400;
    static constexpr int CHIP_HEIGHT = 400;
    static constexpr int PIN_WIDTH = 30;
    static constexpr int PIN_HEIGHT = 10;
    static constexpr int PIN_SPACING = 15;
};

#endif // CHIPVISUALIZER_H