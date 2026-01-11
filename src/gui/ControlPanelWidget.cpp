#include "ControlPanelWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDebug>

ControlPanelWidget::ControlPanelWidget(QWidget *parent)
    : QWidget(parent)
{
    // Crear tabla
    table = new QTableWidget(this);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({"Name", "Pin #", "I/O Value"});

    // Configurar columnas
    table->setColumnWidth(0, 150);  // Name
    table->setColumnWidth(1, 60);   // Pin #
    table->setColumnWidth(2, 200);  // I/O Value (radio buttons)

    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);

    // Layout
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(table);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);
}

ControlPanelWidget::~ControlPanelWidget()
{
    removeAllPins();
}

void ControlPanelWidget::addPin(const std::string& pinName, const std::string& pinNumber)
{
    // Verificar si ya existe
    if (findPinRow(pinName) >= 0) return;

    // Crear fila
    int row = table->rowCount();
    table->insertRow(row);

    // Columna 0: Name
    QTableWidgetItem* nameItem = new QTableWidgetItem(QString::fromStdString(pinName));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, 0, nameItem);

    // Columna 1: Pin #
    QTableWidgetItem* pinItem = new QTableWidgetItem(QString::fromStdString(pinNumber));
    pinItem->setFlags(pinItem->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, 1, pinItem);

    // Columna 2: Radio buttons widget
    QWidget* radioWidget = createRadioButtonWidget(pinName);
    table->setCellWidget(row, 2, radioWidget);
}

QWidget* ControlPanelWidget::createRadioButtonWidget(const std::string& pinName)
{
    QWidget* widget = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(5, 2, 5, 2);

    QRadioButton* radio0 = new QRadioButton("0", widget);
    QRadioButton* radio1 = new QRadioButton("1", widget);
    QRadioButton* radioZ = new QRadioButton("Z", widget);

    // Button group para comportamiento exclusivo
    QButtonGroup* group = new QButtonGroup(widget);
    group->addButton(radio0, 0);
    group->addButton(radio1, 1);
    group->addButton(radioZ, 2);

    // Default: Z
    radioZ->setChecked(true);

    layout->addWidget(radio0);
    layout->addWidget(radio1);
    layout->addWidget(radioZ);
    layout->addStretch();

    widget->setLayout(layout);

    // Conectar señal (guardar pinName en property del widget)
    widget->setProperty("pinName", QString::fromStdString(pinName));

    // Usar idToggled en vez de idClicked para radio buttons
    // idToggled se dispara cuando cambia el estado checked/unchecked
    connect(group, QOverload<int, bool>::of(&QButtonGroup::idToggled),
            this, &ControlPanelWidget::onRadioButtonToggled);

    return widget;
}

void ControlPanelWidget::removePin(const std::string& pinName)
{
    int row = findPinRow(pinName);
    if (row >= 0) {
        table->removeRow(row);
    }
}

void ControlPanelWidget::removeAllPins()
{
    table->setRowCount(0);
}

void ControlPanelWidget::updatePinValue(const std::string& pinName, JTAG::PinLevel level)
{
    int row = findPinRow(pinName);
    if (row < 0) return;

    QWidget* widget = table->cellWidget(row, 2);
    if (!widget) return;

    // Buscar button group en el widget
    QButtonGroup* group = widget->findChild<QButtonGroup*>();
    if (!group) return;

    // Seleccionar radio button según level (sin emitir señal)
    int buttonId = (level == JTAG::PinLevel::LOW) ? 0 :
                   (level == JTAG::PinLevel::HIGH) ? 1 : 2;

    // Bloquear señales temporalmente para evitar loop
    group->blockSignals(true);
    if (group->button(buttonId)) {
        group->button(buttonId)->setChecked(true);
    }
    group->blockSignals(false);
}

void ControlPanelWidget::setEnabled(bool enabled)
{
    table->setEnabled(enabled);

    // Habilitar/deshabilitar todos los radio buttons
    for (int row = 0; row < table->rowCount(); ++row) {
        QWidget* widget = table->cellWidget(row, 2);
        if (widget) {
            widget->setEnabled(enabled);
        }
    }
}

std::string ControlPanelWidget::getSelectedPin() const
{
    int row = table->currentRow();
    if (row < 0 || row >= table->rowCount()) return "";

    QTableWidgetItem* item = table->item(row, 0);
    return item ? item->text().toStdString() : "";
}

void ControlPanelWidget::onRadioButtonToggled(int buttonId, bool checked)
{
    // idToggled se dispara DOS veces: una para el botón que se desmarca (checked=false)
    // y otra para el botón que se marca (checked=true)
    // Solo procesamos cuando checked=true para evitar duplicados
    if (!checked) {
        qDebug() << "[ControlPanel] Button unchecked, ignoring (buttonId:" << buttonId << ")";
        return;
    }

    // Obtener nombre del pin desde el widget que emitió la señal
    QButtonGroup* group = qobject_cast<QButtonGroup*>(sender());
    if (!group) {
        qDebug() << "[ControlPanel] ERROR: sender is not QButtonGroup";
        return;
    }

    QWidget* widget = qobject_cast<QWidget*>(group->parent());
    if (!widget) {
        qDebug() << "[ControlPanel] ERROR: parent is not QWidget";
        return;
    }

    QString pinName = widget->property("pinName").toString();
    if (pinName.isEmpty()) {
        qDebug() << "[ControlPanel] ERROR: pinName property is empty";
        return;
    }

    // Convertir buttonId a PinLevel
    JTAG::PinLevel level;
    QString levelStr;
    switch (buttonId) {
        case 0:
            level = JTAG::PinLevel::LOW;
            levelStr = "LOW (0)";
            break;
        case 1:
            level = JTAG::PinLevel::HIGH;
            levelStr = "HIGH (1)";
            break;
        case 2:
            level = JTAG::PinLevel::HIGH_Z;
            levelStr = "HIGH_Z (Z)";
            break;
        default:
            qDebug() << "[ControlPanel] ERROR: invalid buttonId" << buttonId;
            return;
    }

    qDebug() << "[ControlPanel] Radio button toggled - Pin:" << pinName
             << "Level:" << levelStr << "ButtonId:" << buttonId << "Checked:" << checked;

    // Emitir señal (usando QString directamente, no std::string)
    emit pinValueChanged(pinName, level);
    qDebug() << "[ControlPanel] Signal emitted for pin:" << pinName;
}

int ControlPanelWidget::findPinRow(const std::string& pinName) const
{
    QString qPinName = QString::fromStdString(pinName);
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem* item = table->item(row, 0);
        if (item && item->text() == qPinName) {
            return row;
        }
    }
    return -1;
}
