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

// NUEVO: Para renderFromDeviceModel()
namespace JTAG {
    class DeviceModel;
}

/**
 * @brief Visual state enum for color-coded pin visualization
 */
enum class VisualPinState {
    HIGH,        // Rojo
    LOW,         // Azul
    OSCILLATING, // Amarillo
    UNKNOWN,     // Gris
    LINKAGE      // Negro
};

/**
 * @brief Pin state structure for visualization (legacy)
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
    PinGraphicsItem(const QString& pinNumber, QGraphicsItem* parent = nullptr);

    // Legacy methods (keep for compatibility)
    void setPinState(bool level, bool enabled);
    void setPinName(const QString& name);
    QString getName() const { return m_name; }

    // NEW: Color-coded visualization methods
    void setState(VisualPinState state);
    void setHighlighted(bool highlighted);
    void setLabelRotation(double angle);  // Position label (not rotate text)
    void setPinNumber(const QString& number);
    QString getPinNumber() const { return m_pinNumber; }
    void setCustomDimensions(double width, double height);
    void renderPlaceholder(uint32_t idcode); // Dibuja el chip vacío con el ID
    void setLabelFontSize(int size);



protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    QString m_name;              // Pin logical name (PA0, RESET, etc.)
    QString m_pinNumber;         // Physical pin number (A1, K7, etc.)
    QGraphicsTextItem* m_label;  // Number label
    bool m_level;                // Legacy
    bool m_enabled;              // Legacy
    bool m_hovered;
    VisualPinState m_visualState;
    bool m_highlighted;

    QColor getColorForState(VisualPinState state) const;
    double m_customWidth = 400.0;
    double m_customHeight = 400.0;
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
    void updatePinName(const QString& oldName, const QString& newName);

    // --- ESTA ERA LA FUNCI�N QUE FALTABA DECLARAR ---
    void createLayoutFromController(const std::vector<std::string>& pins);

    // NUEVO: Renderizar desde DeviceModel con layout real
    void renderFromDeviceModel(const JTAG::DeviceModel& model, const QString& customDeviceName = QString());

    // NEW: Color-coded visualization API
    void updatePinState(const QString& pinName, VisualPinState state);
    void highlightPin(const QString& pinName);
    void clearHighlight();
    void setCustomDimensions(double width, double height);
    void renderPlaceholder(uint32_t idcode);

    void setChipSize(double width, double height);

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

    // NUEVO: Estructura para parsear pin numbers
    struct ParsedPin {
        int row;    // 0-based: A=0, B=1, K=10, AA=26
        int col;    // 0-based: 1→0, 7→6
        bool valid;
    };

    void createChipLayout();
    void createLQFP100Layout();
    void addPin(const QString& name, int position, PinSide side);
    double m_chipWidth;
    double m_chipHeight;

    // NUEVO: Helpers para layout real
    ParsedPin parsePinNumber(const QString& pinNumber);
    PinSide determineSide(int row, int col, int maxRow, int maxCol);
    void addPin(const QString& name, const QString& number, double x, double y, PinSide side, double pinSize = 8.0, int fontSize = 7, const QString& type = "");

    QGraphicsScene* m_scene;
    QGraphicsRectItem* m_chipBody;
    QMap<QString, PinGraphicsItem*> m_pins;
    QString m_packageType;

    enum class LayoutMode {
        CENTER_GRID,  // BGA (actual)
        EDGE_PINS     // TQFP (nuevo)
    };
    LayoutMode m_layoutMode = LayoutMode::CENTER_GRID;

    // Layout constants
    static constexpr int CHIP_WIDTH = 400;
    static constexpr int CHIP_HEIGHT = 400;
    static constexpr int PIN_WIDTH = 30;
    static constexpr int PIN_HEIGHT = 10;
    static constexpr int PIN_SPACING = 15;
};

#endif // CHIPVISUALIZER_H