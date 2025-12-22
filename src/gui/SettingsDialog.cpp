#include "SettingsDialog.h"
#include <QFormLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_pollingInterval(100)
    , m_sampleDecimation(1)
{
    setWindowTitle("Performance Settings");
    setMinimumWidth(400);
    setupUI();
}

SettingsDialog::~SettingsDialog()
{
}

void SettingsDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // === POLLING INTERVAL GROUP ===
    QGroupBox *pollingGroup = new QGroupBox("Polling Interval (Refresh Rate)", this);
    QFormLayout *pollingLayout = new QFormLayout(pollingGroup);

    pollingIntervalCombo = new QComboBox(this);
    pollingIntervalCombo->addItem("50 ms", 50);
    pollingIntervalCombo->addItem("100 ms", 100);
    pollingIntervalCombo->addItem("250 ms", 250);
    pollingIntervalCombo->addItem("500 ms", 500);
    pollingIntervalCombo->setCurrentIndex(1);  // Default: 100 ms

    QLabel *pollingDescription = new QLabel(
        "How often to poll the JTAG device and update the UI.\n"
        "Lower values = smoother updates but higher CPU usage.",
        this
    );
    pollingDescription->setWordWrap(true);
    pollingDescription->setStyleSheet("color: gray; font-size: 9pt;");

    pollingLayout->addRow("Interval:", pollingIntervalCombo);
    pollingLayout->addRow("", pollingDescription);

    mainLayout->addWidget(pollingGroup);

    // === SAMPLE DECIMATION GROUP ===
    QGroupBox *decimationGroup = new QGroupBox("Sample Decimation", this);
    QVBoxLayout *decimationLayout = new QVBoxLayout(decimationGroup);

    // Slider + value label
    QHBoxLayout *sliderLayout = new QHBoxLayout();
    QLabel *sliderLabel = new QLabel("Capture:", this);
    sampleDecimationSlider = new QSlider(Qt::Horizontal, this);
    sampleDecimationSlider->setMinimum(1);
    sampleDecimationSlider->setMaximum(100);
    sampleDecimationSlider->setValue(1);
    sampleDecimationSlider->setTickPosition(QSlider::TicksBelow);
    sampleDecimationSlider->setTickInterval(10);

    decimationValueLabel = new QLabel("All samples", this);
    decimationValueLabel->setMinimumWidth(150);
    decimationValueLabel->setStyleSheet("font-weight: bold;");

    sliderLayout->addWidget(sliderLabel);
    sliderLayout->addWidget(sampleDecimationSlider, 1);
    sliderLayout->addWidget(decimationValueLabel);

    decimationLayout->addLayout(sliderLayout);

    QLabel *decimationDescription = new QLabel(
        "Reduce CPU usage by capturing only 1 of every X samples.\n"
        "Example: Value 10 = capture 1 of every 10 samples.",
        this
    );
    decimationDescription->setWordWrap(true);
    decimationDescription->setStyleSheet("color: gray; font-size: 9pt;");
    decimationLayout->addWidget(decimationDescription);

    mainLayout->addWidget(decimationGroup);

    // === DIALOG BUTTONS ===
    buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
        this
    );
    applyButton = buttonBox->button(QDialogButtonBox::Apply);

    mainLayout->addWidget(buttonBox);

    // === CONNECTIONS ===
    connect(pollingIntervalCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onPollingIntervalIndexChanged);
    connect(sampleDecimationSlider, &QSlider::valueChanged,
            this, &SettingsDialog::onSampleDecimationValueChanged);
    connect(applyButton, &QPushButton::clicked,
            this, &SettingsDialog::onApplyClicked);
    connect(buttonBox, &QDialogButtonBox::accepted,
            this, &SettingsDialog::onAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    setLayout(mainLayout);
}

int SettingsDialog::pollingInterval() const
{
    return m_pollingInterval;
}

int SettingsDialog::sampleDecimation() const
{
    return m_sampleDecimation;
}

void SettingsDialog::setPollingInterval(int ms)
{
    m_pollingInterval = ms;

    // Find corresponding index in combobox
    for (int i = 0; i < pollingIntervalCombo->count(); i++) {
        if (pollingIntervalCombo->itemData(i).toInt() == ms) {
            pollingIntervalCombo->setCurrentIndex(i);
            break;
        }
    }
}

void SettingsDialog::setSampleDecimation(int decimation)
{
    m_sampleDecimation = decimation;
    sampleDecimationSlider->setValue(decimation);
    updateDecimationLabel();
}

void SettingsDialog::onPollingIntervalIndexChanged(int index)
{
    m_pollingInterval = pollingIntervalCombo->itemData(index).toInt();
}

void SettingsDialog::onSampleDecimationValueChanged(int value)
{
    m_sampleDecimation = value;
    updateDecimationLabel();
}

void SettingsDialog::updateDecimationLabel()
{
    if (m_sampleDecimation == 1) {
        decimationValueLabel->setText("All samples");
    } else {
        decimationValueLabel->setText(QString("1 of every %1 samples").arg(m_sampleDecimation));
    }
}

void SettingsDialog::onApplyClicked()
{
    // Emit signals without closing dialog
    emit pollingIntervalChanged(m_pollingInterval);
    emit sampleDecimationChanged(m_sampleDecimation);
}

void SettingsDialog::onAccepted()
{
    // Emit signals and close dialog
    emit pollingIntervalChanged(m_pollingInterval);
    emit sampleDecimationChanged(m_sampleDecimation);
    accept();
}
