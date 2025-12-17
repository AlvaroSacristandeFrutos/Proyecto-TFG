#include "ConnectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>

// Usamos el namespace para no escribir JTAG:: todo el rato en el cpp
using namespace JTAG;

ConnectionDialog::ConnectionDialog(const std::vector<AdapterDescriptor>& adapters,
                                   QWidget* parent)
    : QDialog(parent)
    , m_adapterCombo(nullptr)
    , m_clockSpeedCombo(nullptr)
    , m_descriptionLabel(nullptr)
    , m_clockLabel(nullptr)
    , m_btnConnect(nullptr)
    , m_btnCancel(nullptr)
    , m_selectedAdapter(AdapterType::MOCK) // Default safe
{
    setWindowTitle("Connect to JTAG Adapter");
    setModal(true);
    setMinimumWidth(500);  // Más ancho para clock speed

    setupUI(adapters);
}

ConnectionDialog::~ConnectionDialog() {
}

void ConnectionDialog::setupUI(const std::vector<AdapterDescriptor>& adapters) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // === Adapter Selection ===
    QGroupBox* adapterGroup = new QGroupBox("JTAG Adapter", this);
    QVBoxLayout* adapterLayout = new QVBoxLayout(adapterGroup);

    m_adapterCombo = new QComboBox(this);

    // Poblar con adaptadores detectados dinámicamente
    for (const auto& adapter : adapters) {
        QString displayText = QString::fromStdString(
            adapter.name + " - " + adapter.serialNumber
        );
        QVariant userData = QVariant::fromValue(adapter);  // Store full descriptor
        m_adapterCombo->addItem(displayText, userData);
    }

    connect(m_adapterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ConnectionDialog::onAdapterChanged);

    adapterLayout->addWidget(m_adapterCombo);
    mainLayout->addWidget(adapterGroup);

    // === Description ===
    QGroupBox* descGroup = new QGroupBox("Description", this);
    QVBoxLayout* descLayout = new QVBoxLayout(descGroup);

    m_descriptionLabel = new QLabel(this);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setMinimumHeight(100);
    m_descriptionLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    descLayout->addWidget(m_descriptionLabel);

    mainLayout->addWidget(descGroup);

    // === Clock Speed Selection ===
    QGroupBox* clockGroup = new QGroupBox("Clock Speed", this);
    QVBoxLayout* clockLayout = new QVBoxLayout(clockGroup);

    m_clockSpeedCombo = new QComboBox(this);
    m_clockSpeedCombo->addItem("12 MHz", 12000000);
    m_clockSpeedCombo->addItem("6 MHz", 6000000);
    m_clockSpeedCombo->addItem("4 MHz", 4000000);
    m_clockSpeedCombo->addItem("2 MHz", 2000000);
    m_clockSpeedCombo->addItem("1 MHz", 1000000);
    m_clockSpeedCombo->addItem("500 kHz", 500000);
    m_clockSpeedCombo->addItem("100 kHz", 100000);
    m_clockSpeedCombo->addItem("10 kHz", 10000);
    m_clockSpeedCombo->addItem("1 kHz", 1000);
    m_clockSpeedCombo->setCurrentIndex(4); // Default 1 MHz

    clockLayout->addWidget(m_clockSpeedCombo);
    mainLayout->addWidget(clockGroup);

    // === Buttons ===
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_btnConnect = new QPushButton("Connect", this);
    m_btnConnect->setDefault(true);
    connect(m_btnConnect, &QPushButton::clicked, this, &ConnectionDialog::onConnectClicked);

    m_btnCancel = new QPushButton("Cancel", this);
    connect(m_btnCancel, &QPushButton::clicked, this, &ConnectionDialog::onCancelClicked);

    buttonLayout->addWidget(m_btnConnect);
    buttonLayout->addWidget(m_btnCancel);

    mainLayout->addLayout(buttonLayout);

    setLayout(mainLayout);
    updateDescription();
}

void ConnectionDialog::updateDescription() {
    int index = m_adapterCombo->currentIndex();
    QVariant userData = m_adapterCombo->itemData(index);

    if (userData.canConvert<AdapterDescriptor>()) {
        AdapterDescriptor descriptor = userData.value<AdapterDescriptor>();
        AdapterType type = descriptor.type;

        QString description;
        switch (type) {
        case AdapterType::MOCK:
            description = "<b>Mock Adapter</b><br>Simulation for testing.";
            break;
        case AdapterType::JLINK:
            description = "<b>Segger J-Link</b><br>Professional JTAG probe.";
            break;
        case AdapterType::PICO:
            description = "<b>Raspberry Pi Pico</b><br>Low cost USB-JTAG.";
            break;
        default:
            description = "Unknown adapter.";
            break;
        }
        m_descriptionLabel->setText(description);
    }
}

AdapterType ConnectionDialog::getSelectedAdapter() const {
    return m_selectedAdapter;
}

void ConnectionDialog::onAdapterChanged(int index) {
    Q_UNUSED(index);
    updateDescription();
}

void ConnectionDialog::onConnectClicked() {
    int index = m_adapterCombo->currentIndex();
    QVariant userData = m_adapterCombo->itemData(index);

    if (userData.canConvert<AdapterDescriptor>()) {
        m_selectedDescriptor = userData.value<AdapterDescriptor>();
        m_selectedAdapter = m_selectedDescriptor.type;
    }

    accept();
}

void ConnectionDialog::onCancelClicked() {
    reject();
}

uint32_t ConnectionDialog::getSelectedClockSpeed() const {
    return m_clockSpeedCombo->currentData().toUInt();
}

AdapterDescriptor ConnectionDialog::getSelectedDescriptor() const {
    return m_selectedDescriptor;
}