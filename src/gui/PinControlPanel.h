#ifndef PINCONTROLPANEL_H
#define PINCONTROLPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QString>
#include <QList>

// Forward declare from ChipVisualizer.h
struct PinState;

/**
 * @brief Pin control panel widget (TopJTAG style)
 *
 * Displays a table of pins with:
 * - Pin name
 * - Current state (color-coded)
 * - Control buttons (SET/CLR/TOGGLE)
 * - Grouping into buses
 * - Search/filter functionality
 */
class PinControlPanel : public QWidget {
    Q_OBJECT

public:
    explicit PinControlPanel(QWidget* parent = nullptr);
    ~PinControlPanel() override;

    /**
     * @brief Update pin list with current states
     */
    void updatePins(const QList<PinState>& states);

    /**
     * @brief Clear all pins
     */
    void clearPins();

signals:
    /**
     * @brief Emitted when a pin is changed by user
     */
    void pinChanged(const QString& pinName, bool level);

    /**
     * @brief Emitted when bus write is requested
     */
    void busWrite(const QStringList& pins, uint32_t value);

private slots:
    void onPinSetHigh();
    void onPinSetLow();
    void onPinToggle();
    void onBusWriteClicked();
    void onFilterChanged(const QString& filter);
    void onPinSelectionChanged();

private:
    void setupUI();
    void updatePinRow(int row, const PinState& state);
    QString getSelectedPin() const;
    QStringList getSelectedPins() const;

    // === UI Components ===
    QTableWidget* m_pinTable;
    QLineEdit* m_filterEdit;
    QPushButton* m_btnSetHigh;
    QPushButton* m_btnSetLow;
    QPushButton* m_btnToggle;

    // === Bus Control ===
    QLineEdit* m_busValueEdit;
    QComboBox* m_busFormatCombo;
    QPushButton* m_btnBusWrite;

    // === Data ===
    QList<PinState> m_allPins;
};

#endif // PINCONTROLPANEL_H
