#ifndef CONTROLPANELWIDGET_H
#define CONTROLPANELWIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QRadioButton>
#include <QButtonGroup>
#include <vector>
#include <string>
#include "../core/BoundaryScanEngine.h"

namespace JTAG {
    class ScanController;
}

class ControlPanelWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ControlPanelWidget(QWidget *parent = nullptr);
    ~ControlPanelWidget();

    // Gestión de pines
    void addPin(const std::string& pinName, const std::string& pinNumber);
    void removePin(const std::string& pinName);
    void removeAllPins();

    // Actualización de valores (desde backend)
    void updatePinValue(const std::string& pinName, JTAG::PinLevel level);

    // Habilitar/deshabilitar controles
    void setEnabled(bool enabled);

    // Obtener pin seleccionado en tabla
    std::string getSelectedPin() const;

signals:
    // Emitido cuando usuario cambia valor de un pin
    void pinValueChanged(std::string pinName, JTAG::PinLevel level);

private slots:
    void onRadioButtonToggled(int buttonId);

private:
    QTableWidget* table;

    // Helper para crear widget de radio buttons
    QWidget* createRadioButtonWidget(const std::string& pinName);

    // Helper para buscar fila por nombre
    int findPinRow(const std::string& pinName) const;
};

#endif // CONTROLPANELWIDGET_H
