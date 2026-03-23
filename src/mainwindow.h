#pragma once
#include <QMainWindow>
#include <QVector>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QStatusBar>

#include "commontypes.h"
#include "constants.h"
#include "plotwidget.h"
#include "cmdworker.h"
#include "dataworker.h"
#include "waveformgenerator.h"
#include <QComboBox>
#include "waterfallwidget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onGenerate();
    void onLoadDac();
    void onAcquire();
    void onSaveAdc();
    void onDacChannelChanged(int idx);
    void onAdcChannelChanged(int idx);
    void onCaptureReady(WaveformData data);
    void onCmdSequenceDone();
    void onGenerateDone();
    void onExit();

private:
    QWidget *buildMenuPanel();
    void     connectWorkers(const QString &ip);  // (re)create + wire workers
    void     sendCmd(const QString &cmd);
    void     enqueueAcquireSequence();
    void     lockUi();
    void     unlockUi();
    void     setStatus(const QString &s);

    // ── state ─────────────────────────────────────────────────────────────
    int  m_dacCh = 0;
    int  m_adcCh = 0;
    bool m_autoAcquire     = false;
    bool m_waterfallEnabled = false;
    bool m_pendingAcquire  = false;
    bool m_pendingGenerate = false;

    DataRequest        m_pendingDataReq;

    QVector<DacParams> m_dacP;
    QVector<AdcParams> m_adcP;
    QVector<qint16>    m_dacData;
    QVector<qint16>    m_adcData;

    // ── workers ───────────────────────────────────────────────────────────
    CmdWorker  *m_cmdW  = nullptr;
    DataWorker *m_dataW = nullptr;

    // ── plot widgets ──────────────────────────────────────────────────────
    PlotWidget      *m_dacPlot      = nullptr;
    PlotWidget      *m_adcPlot      = nullptr;
    WaterfallWidget *m_dacWaterfall = nullptr;
    WaterfallWidget *m_adcWaterfall = nullptr;

    // ── controls ──────────────────────────────────────────────────────────
    QPushButton *m_btnGen    = nullptr;
    QPushButton *m_btnLoad   = nullptr;
    QPushButton *m_btnAcq    = nullptr;
    QPushButton *m_btnSave   = nullptr;
    QCheckBox   *m_chkAuto   = nullptr;

    QComboBox *m_comboDac       = nullptr;
    QLineEdit *m_dacFreqEdit    = nullptr;
    QLineEdit *m_dacNumEdit     = nullptr;
    QLineEdit *m_dacRateEdit    = nullptr;
    QComboBox *m_comboWaveShape = nullptr;  // Sine / Square
    QLineEdit *m_dacDutyEdit    = nullptr;  // duty cycle 0..1 (square only)

    QComboBox *m_comboAdc    = nullptr;
    QLineEdit *m_adcNumEdit  = nullptr;
    QLineEdit *m_adcRateEdit = nullptr;

    QLabel    *m_statusLbl   = nullptr;
    QLineEdit *m_ipEdit      = nullptr;
};
