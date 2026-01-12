#include "SettingsDialog.h"
#include <QFormLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_pollingInterval(100)
    , m_sampleDecimation(1)
    , m_samplesPerSecond(10)
    , m_waveformFPS(30)
    , m_chipVisFPS(10)
{
    setWindowTitle("Performance Settings");
    setMinimumWidth(450);
    setupUI();
}

SettingsDialog::~SettingsDialog()
{
}

void SettingsDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // === SAMPLING FREQUENCY GROUP ===
    QGroupBox *samplingGroup = new QGroupBox("JTAG Sampling", this);
    QFormLayout *samplingLayout = new QFormLayout(samplingGroup);

    samplesPerSecondSpin = new QSpinBox(this);
    samplesPerSecondSpin->setRange(1, 1000);  // Max teórico con overhead USB
    samplesPerSecondSpin->setValue(10);
    samplesPerSecondSpin->setSuffix(" samples/s");
    samplesPerSecondSpin->setToolTip(
        "How many JTAG TAP cycles per second\n"
        "• Higher = more responsive, more CPU\n"
        "• Lower = less responsive, less CPU\n"
        "• Max 1000 samples/s (USB bandwidth limit)"
    );

    QLabel *samplingDescription = new QLabel(
        "<b>Controls JTAG polling rate.</b><br>"
        "Recommended: 10-100 samples/s for normal use.",
        this
    );
    samplingDescription->setWordWrap(true);
    samplingDescription->setStyleSheet("color: gray; font-size: 9pt;");

    samplingLayout->addRow("Samples/Second:", samplesPerSecondSpin);
    samplingLayout->addRow("", samplingDescription);

    mainLayout->addWidget(samplingGroup);

    // === UI RENDERING GROUP ===
    QGroupBox *renderGroup = new QGroupBox("UI Rendering", this);
    QFormLayout *renderLayout = new QFormLayout(renderGroup);

    // Waveform FPS
    waveformFPSSpin = new QSpinBox(this);
    waveformFPSSpin->setRange(1, 120);
    waveformFPSSpin->setValue(30);
    waveformFPSSpin->setSuffix(" FPS");
    waveformFPSSpin->setToolTip(
        "Waveform redraw rate\n"
        "All data is captured, only rendering throttled"
    );

    QLabel *waveDescription = new QLabel(
        "Redraw rate (all data captured).",
        this
    );
    waveDescription->setStyleSheet("color: gray; font-size: 9pt;");
    renderLayout->addRow("Waveform FPS:", waveformFPSSpin);
    renderLayout->addRow("", waveDescription);

    // ChipVisualizer FPS
    chipVisFPSSpin = new QSpinBox(this);
    chipVisFPSSpin->setRange(1, 120);
    chipVisFPSSpin->setValue(10);
    chipVisFPSSpin->setSuffix(" FPS");
    chipVisFPSSpin->setToolTip(
        "Chip visualization redraw rate\n"
        "All pin changes captured, only rendering throttled"
    );

    QLabel *chipDescription = new QLabel(
        "Redraw rate (all changes captured).",
        this
    );
    chipDescription->setStyleSheet("color: gray; font-size: 9pt;");
    renderLayout->addRow("Chip Visualizer FPS:", chipVisFPSSpin);
    renderLayout->addRow("", chipDescription);

    mainLayout->addWidget(renderGroup);

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

    // === INFO LABEL ===
    QLabel *infoLabel = new QLabel(
        "<b>Note:</b><br>"
        "• <b>Samples/Second</b>: Controls JTAG polling (CPU intensive)<br>"
        "• <b>FPS settings</b>: Only affect rendering, all data is captured",
        this
    );
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 8px; border-radius: 4px; }");
    mainLayout->addWidget(infoLabel);

    // === DIALOG BUTTONS ===
    buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
        this
    );
    applyButton = buttonBox->button(QDialogButtonBox::Apply);

    mainLayout->addWidget(buttonBox);

    // === CONNECTIONS ===
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

int SettingsDialog::samplesPerSecond() const
{
    return m_samplesPerSecond;
}

int SettingsDialog::waveformFPS() const
{
    return m_waveformFPS;
}

int SettingsDialog::chipVisFPS() const
{
    return m_chipVisFPS;
}

void SettingsDialog::setPollingInterval(int ms)
{
    m_pollingInterval = ms;
}

void SettingsDialog::setSampleDecimation(int decimation)
{
    m_sampleDecimation = decimation;
    sampleDecimationSlider->setValue(decimation);
    updateDecimationLabel();
}

void SettingsDialog::setSamplesPerSecond(int samplesPerSec)
{
    m_samplesPerSecond = samplesPerSec;
    samplesPerSecondSpin->setValue(samplesPerSec);
}

void SettingsDialog::setWaveformFPS(int fps)
{
    m_waveformFPS = fps;
    waveformFPSSpin->setValue(fps);
}

void SettingsDialog::setChipVisFPS(int fps)
{
    m_chipVisFPS = fps;
    chipVisFPSSpin->setValue(fps);
}

void SettingsDialog::onPollingIntervalIndexChanged(int index)
{
    // Legacy function - not used anymore
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
    // Read current values from spinboxes
    m_samplesPerSecond = samplesPerSecondSpin->value();
    m_waveformFPS = waveformFPSSpin->value();
    m_chipVisFPS = chipVisFPSSpin->value();

    // Emit signals without closing dialog
    emit sampleDecimationChanged(m_sampleDecimation);
    emit samplesPerSecondChanged(m_samplesPerSecond);
    emit waveformFPSChanged(m_waveformFPS);
    emit chipVisFPSChanged(m_chipVisFPS);
}

void SettingsDialog::onAccepted()
{
    // Read current values from spinboxes
    m_samplesPerSecond = samplesPerSecondSpin->value();
    m_waveformFPS = waveformFPSSpin->value();
    m_chipVisFPS = chipVisFPSSpin->value();

    // Emit signals and close dialog
    emit sampleDecimationChanged(m_sampleDecimation);
    emit samplesPerSecondChanged(m_samplesPerSecond);
    emit waveformFPSChanged(m_waveformFPS);
    emit chipVisFPSChanged(m_chipVisFPS);
    accept();
}
