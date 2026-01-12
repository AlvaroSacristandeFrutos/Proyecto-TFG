#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>

/**
 * @brief Settings dialog for performance configuration
 *
 * Allows user to configure:
 * - Samples per second: 1-1000 (how often JTAG is polled)
 * - Waveform FPS: 10-60 (rendering rate, all data captured)
 * - ChipVisualizer FPS: 5-30 (rendering rate, all changes captured)
 * - Sample decimation: capture 1 of every X samples (1-100)
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    // Getters
    int pollingInterval() const;
    int sampleDecimation() const;
    int samplesPerSecond() const;
    int waveformFPS() const;
    int chipVisFPS() const;

    // Setters
    void setPollingInterval(int ms);
    void setSampleDecimation(int decimation);
    void setSamplesPerSecond(int samplesPerSec);
    void setWaveformFPS(int fps);
    void setChipVisFPS(int fps);

signals:
    void pollingIntervalChanged(int ms);
    void sampleDecimationChanged(int decimation);
    void samplesPerSecondChanged(int samplesPerSec);
    void waveformFPSChanged(int fps);
    void chipVisFPSChanged(int fps);

private slots:
    void onPollingIntervalIndexChanged(int index);
    void onSampleDecimationValueChanged(int value);
    void onApplyClicked();
    void onAccepted();

private:
    void setupUI();
    void updateDecimationLabel();

    // UI components
    QComboBox *pollingIntervalCombo;
    QSlider *sampleDecimationSlider;
    QLabel *decimationValueLabel;
    QSpinBox *samplesPerSecondSpin;
    QSpinBox *waveformFPSSpin;
    QSpinBox *chipVisFPSSpin;
    QDialogButtonBox *buttonBox;
    QPushButton *applyButton;

    // Current values
    int m_pollingInterval;
    int m_sampleDecimation;
    int m_samplesPerSecond;
    int m_waveformFPS;
    int m_chipVisFPS;
};

#endif // SETTINGSDIALOG_H
