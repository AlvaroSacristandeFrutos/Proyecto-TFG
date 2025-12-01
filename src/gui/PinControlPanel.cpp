#include "PinControlPanel.h"
#include "ChipVisualizer.h" 
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QColor>
#include <QBrush>
#include <QRegularExpression> // CORRECCIÓN: Usar QRegularExpression en Qt6

PinControlPanel::PinControlPanel(QWidget* parent)
    : QWidget(parent)
    , m_pinTable(nullptr)
    , m_filterEdit(nullptr)
    , m_btnSetHigh(nullptr)
    , m_btnSetLow(nullptr)
    , m_btnToggle(nullptr)
    , m_busValueEdit(nullptr)
    , m_busFormatCombo(nullptr)
    , m_btnBusWrite(nullptr)
{
    setupUI();
}

PinControlPanel::~PinControlPanel() {
}

void PinControlPanel::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Filter
    QGroupBox* filterGroup = new QGroupBox("Filter", this);
    QHBoxLayout* filterLayout = new QHBoxLayout(filterGroup);
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Search pins (e.g., PA*, PB3)...");
    filterLayout->addWidget(m_filterEdit);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &PinControlPanel::onFilterChanged);
    mainLayout->addWidget(filterGroup);

    // Pin Table
    m_pinTable = new QTableWidget(0, 3, this);
    m_pinTable->setHorizontalHeaderLabels({ "Pin Name", "State", "Control" });
    m_pinTable->horizontalHeader()->setStretchLastSection(true);
    m_pinTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pinTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pinTable->setAlternatingRowColors(true);
    m_pinTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pinTable->verticalHeader()->setVisible(false);
    m_pinTable->setColumnWidth(0, 80);
    m_pinTable->setColumnWidth(1, 60);
    connect(m_pinTable, &QTableWidget::itemSelectionChanged, this, &PinControlPanel::onPinSelectionChanged);
    mainLayout->addWidget(m_pinTable, 1);

    // Pin Control
    QGroupBox* pinControlGroup = new QGroupBox("Pin Control", this);
    QHBoxLayout* pinControlLayout = new QHBoxLayout(pinControlGroup);
    m_btnSetHigh = new QPushButton("SET (1)", this);
    m_btnSetHigh->setEnabled(false);
    m_btnSetHigh->setStyleSheet("QPushButton { background-color: #90EE90; }");
    connect(m_btnSetHigh, &QPushButton::clicked, this, &PinControlPanel::onPinSetHigh);
    m_btnSetLow = new QPushButton("CLR (0)", this);
    m_btnSetLow->setEnabled(false);
    m_btnSetLow->setStyleSheet("QPushButton { background-color: #D3D3D3; }");
    connect(m_btnSetLow, &QPushButton::clicked, this, &PinControlPanel::onPinSetLow);
    m_btnToggle = new QPushButton("TOGGLE", this);
    m_btnToggle->setEnabled(false);
    connect(m_btnToggle, &QPushButton::clicked, this, &PinControlPanel::onPinToggle);
    pinControlLayout->addWidget(m_btnSetHigh);
    pinControlLayout->addWidget(m_btnSetLow);
    pinControlLayout->addWidget(m_btnToggle);
    mainLayout->addWidget(pinControlGroup);

    // Bus Control
    QGroupBox* busControlGroup = new QGroupBox("Bus Write", this);
    QVBoxLayout* busLayout = new QVBoxLayout(busControlGroup);
    QHBoxLayout* busInputLayout = new QHBoxLayout();
    busInputLayout->addWidget(new QLabel("Value:", this));
    m_busValueEdit = new QLineEdit(this);
    m_busValueEdit->setPlaceholderText("Enter value...");
    busInputLayout->addWidget(m_busValueEdit);
    m_busFormatCombo = new QComboBox(this);
    m_busFormatCombo->addItems({ "Hex", "Bin", "Dec" });
    busInputLayout->addWidget(m_busFormatCombo);
    busLayout->addLayout(busInputLayout);
    m_btnBusWrite = new QPushButton("Write Bus", this);
    m_btnBusWrite->setEnabled(false);
    connect(m_btnBusWrite, &QPushButton::clicked, this, &PinControlPanel::onBusWriteClicked);
    busLayout->addWidget(m_btnBusWrite);
    mainLayout->addWidget(busControlGroup);

    mainLayout->addStretch();
    setLayout(mainLayout);
}

void PinControlPanel::updatePins(const QList<PinState>& states) {
    m_allPins = states;
    m_pinTable->setRowCount(0);
    QString filter = m_filterEdit->text().trimmed();

    // --- CORRECCIÓN: Lógica de filtrado con QRegularExpression (Qt6) ---
    QRegularExpression regex;
    if (!filter.isEmpty()) {
        QString pattern = QRegularExpression::escape(filter);
        pattern.replace("\\*", ".*"); // Permitir wildcards simples
        regex.setPattern(pattern);
        regex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    }

    for (int i = 0; i < states.size(); ++i) {
        const PinState& state = states[i];
        if (!filter.isEmpty()) {
            if (!regex.match(state.name).hasMatch()) continue;
        }

        int row = m_pinTable->rowCount();
        m_pinTable->insertRow(row);
        updatePinRow(row, state);
    }
}

void PinControlPanel::updatePinRow(int row, const PinState& state) {
    QTableWidgetItem* nameItem = new QTableWidgetItem(state.name);
    nameItem->setData(Qt::UserRole, state.name);
    m_pinTable->setItem(row, 0, nameItem);

    QTableWidgetItem* stateItem = new QTableWidgetItem(state.level ? "HIGH (1)" : "LOW (0)");
    if (state.enabled) {
        if (state.level) {
            stateItem->setBackground(QBrush(QColor(144, 238, 144)));
            stateItem->setForeground(QBrush(Qt::black));
        }
        else {
            stateItem->setBackground(QBrush(QColor(100, 100, 100)));
            stateItem->setForeground(QBrush(Qt::white));
        }
    }
    else {
        stateItem->setBackground(QBrush(QColor(211, 211, 211)));
        stateItem->setForeground(QBrush(Qt::black));
    }
    m_pinTable->setItem(row, 1, stateItem);

    QString controlText = state.enabled ? "Controllable" : "Read-only";
    QTableWidgetItem* controlItem = new QTableWidgetItem(controlText);
    controlItem->setForeground(QBrush(state.enabled ? Qt::darkGreen : Qt::gray));
    m_pinTable->setItem(row, 2, controlItem);
}

void PinControlPanel::clearPins() {
    m_pinTable->setRowCount(0);
    m_allPins.clear();
}

QString PinControlPanel::getSelectedPin() const {
    QList<QTableWidgetItem*> selected = m_pinTable->selectedItems();
    if (selected.isEmpty()) return QString();
    int row = selected.first()->row();
    QTableWidgetItem* nameItem = m_pinTable->item(row, 0);
    return nameItem ? nameItem->data(Qt::UserRole).toString() : QString();
}

QStringList PinControlPanel::getSelectedPins() const {
    QStringList pins;
    QList<QTableWidgetItem*> selected = m_pinTable->selectedItems();
    QSet<int> rows;
    for (auto* item : selected) rows.insert(item->row());
    for (int row : rows) {
        QTableWidgetItem* nameItem = m_pinTable->item(row, 0);
        if (nameItem) pins.append(nameItem->data(Qt::UserRole).toString());
    }
    return pins;
}

void PinControlPanel::onPinSetHigh() {
    QString pin = getSelectedPin();
    if (!pin.isEmpty()) emit pinChanged(pin, true);
}

void PinControlPanel::onPinSetLow() {
    QString pin = getSelectedPin();
    if (!pin.isEmpty()) emit pinChanged(pin, false);
}

void PinControlPanel::onPinToggle() {
    QString pin = getSelectedPin();
    if (!pin.isEmpty()) {
        for (const auto& state : m_allPins) {
            if (state.name == pin) {
                emit pinChanged(pin, !state.level);
                break;
            }
        }
    }
}

void PinControlPanel::onBusWriteClicked() {
    QStringList pins = getSelectedPins();
    if (pins.isEmpty()) return;
    QString valueStr = m_busValueEdit->text().trimmed();
    if (valueStr.isEmpty()) return;

    uint32_t value = 0;
    bool ok = false;
    QString format = m_busFormatCombo->currentText();

    if (format == "Hex") {
        if (valueStr.startsWith("0x", Qt::CaseInsensitive)) valueStr = valueStr.mid(2);
        value = valueStr.toUInt(&ok, 16);
    }
    else if (format == "Bin") {
        if (valueStr.startsWith("0b", Qt::CaseInsensitive)) valueStr = valueStr.mid(2);
        value = valueStr.toUInt(&ok, 2);
    }
    else {
        value = valueStr.toUInt(&ok, 10);
    }

    if (ok) emit busWrite(pins, value);
}

void PinControlPanel::onFilterChanged(const QString& filter) {
    Q_UNUSED(filter);
    updatePins(m_allPins);
}

void PinControlPanel::onPinSelectionChanged() {
    QStringList selectedPins = getSelectedPins();
    bool multiple = selectedPins.size() > 1;
    bool has = !selectedPins.isEmpty();
    m_btnSetHigh->setEnabled(has && !multiple);
    m_btnSetLow->setEnabled(has && !multiple);
    m_btnToggle->setEnabled(has && !multiple);
    m_btnBusWrite->setEnabled(multiple);
}