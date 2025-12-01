#include "ConnectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>

// Usamos el namespace para no escribir JTAG:: todo el rato en el cpp
using namespace JTAG;

ConnectionDialog::ConnectionDialog(QWidget* parent)
    : QDialog(parent)
    , m_adapterCombo(nullptr)
    , m_descriptionLabel(nullptr)
    , m_btnConnect(nullptr)
    , m_btnCancel(nullptr)
    , m_selectedAdapter(AdapterType::MOCK) // Default safe
{
    setWindowTitle("Connect to JTAG Adapter");
    setModal(true);
    setMinimumWidth(400);

    setupUI();
}

ConnectionDialog::~ConnectionDialog() {
}

void ConnectionDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // === Adapter Selection ===
    QGroupBox* adapterGroup = new QGroupBox("Select Adapter", this);
    QVBoxLayout* adapterLayout = new QVBoxLayout(adapterGroup);

    m_adapterCombo = new QComboBox(this);
    // Ahora funciona porque pusimos 'using namespace JTAG' arriba
    m_adapterCombo->addItem("Mock Adapter (Testing)", static_cast<int>(AdapterType::MOCK));
    m_adapterCombo->addItem("Segger J-Link", static_cast<int>(AdapterType::JLINK));
    // m_adapterCombo->addItem("FTDI FT2232H (MPSSE)", static_cast<int>(AdapterType::FTDI)); // Si lo tienes definido
    m_adapterCombo->addItem("Raspberry Pi Pico (Custom)", static_cast<int>(AdapterType::PICO));

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
    // Casting seguro
    AdapterType type = static_cast<AdapterType>(m_adapterCombo->itemData(index).toInt());

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

AdapterType ConnectionDialog::getSelectedAdapter() const {
    return m_selectedAdapter;
}

void ConnectionDialog::onAdapterChanged(int index) {
    Q_UNUSED(index);
    updateDescription();
}

void ConnectionDialog::onConnectClicked() {
    int index = m_adapterCombo->currentIndex();
    m_selectedAdapter = static_cast<AdapterType>(m_adapterCombo->itemData(index).toInt());
    accept();
}

void ConnectionDialog::onCancelClicked() {
    reject();
}