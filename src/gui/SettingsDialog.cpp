#include "SettingsDialog.h"
#include <QFormLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_pollingInterval(100)
    , m_sampleDecimation(1)
    , m_samplesPerSecond(10)
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
    pollingIntervalCombo->addItem("1 ms", 1);
    pollingIntervalCombo->addItem("5 ms", 5);
    pollingIntervalCombo->addItem("10 ms", 10);
    pollingIntervalCombo->addItem("50 ms", 50);
    pollingIntervalCombo->addItem("100 ms", 100);
    pollingIntervalCombo->addItem("250 ms", 250);
    pollingIntervalCombo->addItem("500 ms", 500);
    pollingIntervalCombo->setCurrentIndex(4);  // Default: 100 ms

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

    // === SAMPLING FREQUENCY GROUP ===
    QGroupBox *samplingGroup = new QGroupBox("Sampling Frequency", this);
    QFormLayout *samplingLayout = new QFormLayout(samplingGroup);

    samplesPerSecondCombo = new QComboBox(this);
    samplesPerSecondCombo->addItem("1 samples/s", 1);
    samplesPerSecondCombo->addItem("5 samples/s", 5);
    samplesPerSecondCombo->addItem("10 samples/s (Recommended)", 10);
    samplesPerSecondCombo->addItem("50 samples/s", 50);
    samplesPerSecondCombo->addItem("100 samples/s", 100);
    samplesPerSecondCombo->addItem("500 samples/s", 500);
    samplesPerSecondCombo->addItem("1000 samples/s (CRITICAL MAX)", 1000);
    samplesPerSecondCombo->setCurrentIndex(2);  // Default: 10 samples/s

    QLabel *samplingDescription = new QLabel(
        "How many times per second the probe updates.\n"
        "• Higher values = more frequent updates, more CPU usage\n"
        "• Lower values = less frequent, reduced load\n"
        "• MAX 1000 samples/s is critical limit",
        this
    );
    samplingDescription->setWordWrap(true);
    samplingDescription->setStyleSheet("color: gray; font-size: 9pt;");

    samplingLayout->addRow("Frequency:", samplesPerSecondCombo);
    samplingLayout->addRow("", samplingDescription);

    mainLayout->addWidget(samplingGroup);

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
    connect(samplesPerSecondCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onSamplesPerSecondIndexChanged);
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

int SettingsDialog::samplesPerSecond() const
{
    return m_samplesPerSecond;
}

void SettingsDialog::setSamplesPerSecond(int samplesPerSec)
{
    m_samplesPerSecond = samplesPerSec;
    for (int i = 0; i < samplesPerSecondCombo->count(); ++i) {
        if (samplesPerSecondCombo->itemData(i).toInt() == samplesPerSec) {
            samplesPerSecondCombo->setCurrentIndex(i);
            break;
        }
    }
}

void SettingsDialog::onSamplesPerSecondIndexChanged(int index)
{
    m_samplesPerSecond = samplesPerSecondCombo->itemData(index).toInt();
}

void SettingsDialog::onApplyClicked()
{
    // Emit signals without closing dialog
    emit pollingIntervalChanged(m_pollingInterval);
    emit sampleDecimationChanged(m_sampleDecimation);
    emit samplesPerSecondChanged(m_samplesPerSecond);
}

void SettingsDialog::onAccepted()
{
    // Emit signals and close dialog
    emit pollingIntervalChanged(m_pollingInterval);
    emit sampleDecimationChanged(m_sampleDecimation);
    emit samplesPerSecondChanged(m_samplesPerSecond);
    accept();
}
