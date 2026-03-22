#include "mainwindow.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QStatusBar>
#include <QFile>
#include <QDebug>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Style helpers
// ─────────────────────────────────────────────────────────────────────────────

static const QString COMBO_STYLE =
    "QComboBox{background:#313244;color:#cdd6f4;border:1px solid #45475a;"
    "border-radius:3px;padding:2px 5px;}"
    "QComboBox QAbstractItemView{background:#313244;color:#cdd6f4;"
    "selection-background-color:#45475a;}";

static void styleGreen(QPushButton *b)
{
    b->setStyleSheet(
        "QPushButton{background:#27ae60;color:white;font-weight:bold;"
        "border-radius:4px;padding:4px 10px;}"
        "QPushButton:hover{background:#2ecc71;}"
        "QPushButton:disabled{background:#555;color:#999;}");
}

static void styleRed(QPushButton *b)
{
    b->setStyleSheet(
        "QPushButton{background:#c0392b;color:white;font-weight:bold;"
        "border-radius:4px;padding:4px 10px;}"
        "QPushButton:hover{background:#e74c3c;}");
}

static QLineEdit *makeField(const QString &val, bool readOnly = false)
{
    auto *e = new QLineEdit(val);
    e->setReadOnly(readOnly);
    e->setStyleSheet(
        "QLineEdit{background:#313244;color:#cdd6f4;border:1px solid #45475a;"
        "border-radius:3px;padding:2px 5px;}"
        "QLineEdit:read-only{color:#888;}");
    return e;
}

static QLabel *makeLabel(const QString &text)
{
    auto *l = new QLabel(text);
    l->setStyleSheet("color:#a6adc8;font-size:11px;");
    return l;
}

static QFrame *makeSep()
{
    auto *f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    f->setStyleSheet("color:#45475a;");
    return f;
}

static QStringList channelNames(const QString &prefix)
{
    QStringList l;
    for (int i = 0; i < 16; ++i)
        l << QString("Tile%1  %2%3").arg(i / 4).arg(prefix).arg(i % 4);
    return l;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Avnet RFSoC Evaluation Tool");
    resize(1600, 950);
    setStyleSheet("QMainWindow,QWidget{background:#1e1e2e;color:#cdd6f4;}"
                  "QStatusBar{background:#181825;}");

    m_dacP.resize(Constant::NUM_CHANNELS);
    m_adcP.resize(Constant::NUM_CHANNELS);
    for (auto &p : m_dacP) {
        p.sampleRate = Constant::DAC_FS;
        p.sampleNum  = Constant::DAC_SAMPLE_NUM;
    }
    for (auto &p : m_adcP) {
        p.sampleRate = Constant::ADC_FS;
        p.sampleNum  = Constant::ADC_SAMPLE_NUM;
    }

    m_cmdW  = new CmdWorker (Constant::BOARD_IP, Constant::CMD_PORT,  this);
    m_dataW = new DataWorker(Constant::BOARD_IP, Constant::DATA_PORT, this);

    connect(m_dataW, &DataWorker::captureReady,
            this,    &MainWindow::onCaptureReady);
    connect(m_dataW, &DataWorker::sendDone,
            this,    &MainWindow::onGenerateDone);
    connect(m_cmdW,  &CmdWorker::sequenceDone,
            this,    &MainWindow::onCmdSequenceDone);
    connect(m_cmdW,  &CmdWorker::errorOccurred,
            this, [this](const QString &e){ setStatus("CMD: " + e); });
    connect(m_dataW, &DataWorker::errorOccurred,
            this, [this](const QString &e){ setStatus("DATA: " + e); unlockUi(); });

    m_cmdW->start();
    m_dataW->start();

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);
    root->addWidget(buildMenuPanel());

    m_dacPlot = new PlotWidget("DAC", central);
    root->addWidget(m_dacPlot, 1);
    m_adcPlot = new PlotWidget("ADC", central);
    root->addWidget(m_adcPlot, 1);

    m_statusLbl = new QLabel("Ready");
    m_statusLbl->setStyleSheet("color:#a6e3a1; font-size:11px;");
    statusBar()->addWidget(m_statusLbl, 1);

    // Initial dummy waveforms
    int trimmed = 0;
    const DacParams &dp = m_dacP[0];
    m_dacData = WaveformGenerator::generateToneNormalized(
        dp.freq, dp.theta, dp.sampleRate, dp.sampleNum, trimmed);
    m_dacP[0].sampleNumTrimmed = trimmed;
    m_dacPlot->updateWaveform(m_dacData, dp.sampleRate);

    const AdcParams &ap = m_adcP[0];
    m_adcData = WaveformGenerator::generateToneNormalized(
        100000000LL, 0.0, ap.sampleRate, ap.sampleNum, trimmed);
    m_adcPlot->updateWaveform(m_adcData, ap.sampleRate);
}

MainWindow::~MainWindow()
{
    m_cmdW->stop();
    m_dataW->stop();
    m_cmdW->wait(2000);
    m_dataW->wait(2000);
}

// ─────────────────────────────────────────────────────────────────────────────
// Menu panel
// ─────────────────────────────────────────────────────────────────────────────

QWidget *MainWindow::buildMenuPanel()
{
    auto *panel = new QWidget;
    panel->setFixedWidth(265);
    panel->setStyleSheet("QWidget{background:#181825;}");
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(5);

    auto *title = new QLabel("RFSoC Tool");
    title->setStyleSheet("color:#89b4fa;font-size:16px;font-weight:bold;");
    title->setAlignment(Qt::AlignHCenter);
    lay->addWidget(title);
    lay->addWidget(makeSep());

    // DAC
    {
        auto *hdr = new QLabel("── DAC ──");
        hdr->setStyleSheet("color:#cba6f7;font-weight:bold;");
        hdr->setAlignment(Qt::AlignHCenter);
        lay->addWidget(hdr);

        lay->addWidget(makeLabel("Channel"));
        m_comboDac = new QComboBox;
        m_comboDac->addItems(channelNames("DAC"));
        m_comboDac->setStyleSheet(COMBO_STYLE);
        connect(m_comboDac, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onDacChannelChanged);
        lay->addWidget(m_comboDac);

        lay->addWidget(makeLabel("Freq (Hz)"));
        m_dacFreqEdit = makeField(QString::number(m_dacP[0].freq));
        lay->addWidget(m_dacFreqEdit);

        lay->addWidget(makeLabel("Sample Num"));
        m_dacNumEdit = makeField(QString::number(m_dacP[0].sampleNum), true);
        lay->addWidget(m_dacNumEdit);

        lay->addWidget(makeLabel("Rate (GSPS)"));
        m_dacRateEdit = makeField(
            QString::number(m_dacP[0].sampleRate / 1e9, 'f', 4), true);
        lay->addWidget(m_dacRateEdit);

        auto *row = new QHBoxLayout;
        m_btnGen  = new QPushButton("Generate"); styleGreen(m_btnGen);
        m_btnLoad = new QPushButton("Load");     styleGreen(m_btnLoad);
        connect(m_btnGen,  &QPushButton::clicked, this, &MainWindow::onGenerate);
        connect(m_btnLoad, &QPushButton::clicked, this, &MainWindow::onLoadDac);
        row->addWidget(m_btnGen);
        row->addWidget(m_btnLoad);
        lay->addLayout(row);
    }

    lay->addWidget(makeSep());

    // ADC
    {
        auto *hdr = new QLabel("── ADC ──");
        hdr->setStyleSheet("color:#cba6f7;font-weight:bold;");
        hdr->setAlignment(Qt::AlignHCenter);
        lay->addWidget(hdr);

        lay->addWidget(makeLabel("Channel"));
        m_comboAdc = new QComboBox;
        m_comboAdc->addItems(channelNames("ADC"));
        m_comboAdc->setStyleSheet(COMBO_STYLE);
        connect(m_comboAdc, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onAdcChannelChanged);
        lay->addWidget(m_comboAdc);

        lay->addWidget(makeLabel("Sample Num"));
        m_adcNumEdit = makeField(QString::number(m_adcP[0].sampleNum), true);
        lay->addWidget(m_adcNumEdit);

        lay->addWidget(makeLabel("Rate (GSPS)"));
        m_adcRateEdit = makeField(
            QString::number(m_adcP[0].sampleRate / 1e9, 'f', 4), true);
        lay->addWidget(m_adcRateEdit);

        auto *row = new QHBoxLayout;
        m_btnAcq  = new QPushButton("Acquire"); styleGreen(m_btnAcq);
        m_btnSave = new QPushButton("Save");    styleGreen(m_btnSave);
        connect(m_btnAcq,  &QPushButton::clicked, this, &MainWindow::onAcquire);
        connect(m_btnSave, &QPushButton::clicked, this, &MainWindow::onSaveAdc);
        row->addWidget(m_btnAcq);
        row->addWidget(m_btnSave);
        lay->addLayout(row);

        m_chkAuto = new QCheckBox("Auto Acquire");
        m_chkAuto->setStyleSheet("QCheckBox{color:#cdd6f4;}");
        connect(m_chkAuto, &QCheckBox::toggled,
                this, [this](bool v){ m_autoAcquire = v; });
        lay->addWidget(m_chkAuto);
    }

    lay->addWidget(makeSep());

    auto *btnExit = new QPushButton("Exit"); styleRed(btnExit);
    connect(btnExit, &QPushButton::clicked, this, &MainWindow::onExit);
    lay->addWidget(btnExit);
    lay->addStretch();
    return panel;
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots — DAC
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::onDacChannelChanged(int idx)
{
    m_dacCh = idx;
    const DacParams &p = m_dacP[idx];
    m_dacFreqEdit->setText(QString::number(p.freq));
    m_dacNumEdit->setText(QString::number(p.sampleNum));
    m_dacRateEdit->setText(QString::number(p.sampleRate / 1e9, 'f', 4));
}

void MainWindow::onGenerate()
{
    bool ok = false;
    qint64 freq = m_dacFreqEdit->text().toLongLong(&ok);
    if (!ok || freq <= 0) {
        QMessageBox::critical(this, "Input Error", "Enter a valid frequency in Hz.");
        return;
    }

    DacParams &p = m_dacP[m_dacCh];
    p.freq = freq;

    int trimmed = 0;
    m_dacData = WaveformGenerator::generateToneNormalized(
        p.freq, p.theta, p.sampleRate, p.sampleNum, trimmed);
    p.sampleNumTrimmed = trimmed;
    m_dacPlot->updateWaveform(m_dacData, p.sampleRate);

    // Save TX request — fired in onCmdSequenceDone after CMD sequence finishes
    const int ch = m_dacCh;
    m_pendingDataReq.type    = DataRequest::SendWaveform;
    m_pendingDataReq.command = QString("WriteDataToMemoryFromSocket 3 %1 %2 %3 0\r\n")
                                   .arg(ch / 4).arg(ch % 4).arg(p.sampleNum * 2).toUtf8();
    m_pendingDataReq.payload = QByteArray(
        reinterpret_cast<const char *>(m_dacData.constData()),
        m_dacData.size() * static_cast<int>(sizeof(qint16)));
    m_pendingGenerate = true;

    lockUi();

    // CMD sequence with 100 ms delay between each command (same as Python sleep(0.1))
    QList<CmdItem> seq;
    seq << CmdItem{ "TermMode 0\r\n",                                              100, false }
        << CmdItem{ "LocalMemInfo 1\r\n",                                          100, false }
        << CmdItem{ "LocalMemTrigger 1 0 0 0x0\r\n",                              100, false }
        << CmdItem{ QString("SetLocalMemSample 1 %1 %2 %3\r\n")
                        .arg(ch/4).arg(ch%4).arg(p.sampleNumTrimmed).toUtf8(),     100, true  };
    //                                                                                   ^^^^
    //                                                         finalItem=true → emits sequenceDone
    m_cmdW->enqueueSequence(seq);
    setStatus(QString("Preparing DAC ch%1 …").arg(ch));
}

void MainWindow::onLoadDac()
{
    const QString fname = QFileDialog::getOpenFileName(
        this, "Load DAC binary", {},
        "Binary files (*.bin);;All files (*.*)");
    if (fname.isEmpty()) return;

    QFile f(fname);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Error", f.errorString()); return;
    }
    const QByteArray raw = f.readAll();
    const int n = raw.size() / static_cast<int>(sizeof(qint16));
    if (n == 0) { QMessageBox::warning(this, "Warning", "File empty."); return; }
    m_dacData.resize(n);
    memcpy(m_dacData.data(), raw.constData(), n * sizeof(qint16));
    m_dacP[m_dacCh].sampleNum = n;
    m_dacNumEdit->setText(QString::number(n));
    m_dacPlot->updateWaveform(m_dacData, m_dacP[m_dacCh].sampleRate);
    setStatus("Loaded: " + fname);
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots — ADC
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::onAdcChannelChanged(int idx)
{
    m_adcCh = idx;
    const AdcParams &p = m_adcP[idx];
    m_adcNumEdit->setText(QString::number(p.sampleNum));
    m_adcRateEdit->setText(QString::number(p.sampleRate / 1e9, 'f', 4));
}

void MainWindow::onAcquire()
{
    lockUi();
    enqueueAcquireSequence();
}

void MainWindow::enqueueAcquireSequence()
{
    const int ch = m_adcCh;
    const AdcParams &p = m_adcP[ch];

    // Save RX request — fired in onCmdSequenceDone after CMD sequence finishes
    m_pendingDataReq.type          = DataRequest::ReceiveCapture;
    m_pendingDataReq.command       = QString("ReadDataFromMemoryToSocket 3 %1 %2 %3 0\r\n")
                                         .arg(ch / 4).arg(ch % 4).arg(p.sampleNum * 2).toUtf8();
    m_pendingDataReq.expectedBytes = p.sampleNum * 2;
    m_pendingDataReq.sampleRate    = p.sampleRate;
    m_pendingDataReq.channel       = ch;
    m_pendingAcquire = true;

    // CMD sequence with 100 ms delay between each command
    QList<CmdItem> seq;
    seq << CmdItem{ "TermMode 0\r\n",                                              100, false }
        << CmdItem{ QString("SetLocalMemSample 0 %1 %2 %3\r\n")
                        .arg(ch/4).arg(ch%4).arg(p.sampleNum).toUtf8(),            100, false }
        << CmdItem{ "LocalMemInfo 0\r\n",                                          100, false }
        << CmdItem{ QString("LocalMemTrigger 0 0 %1 0x%2\r\n")
                        .arg(p.sampleNum).arg(1 << ch, 0, 16).toUtf8(),            100, true  };
    m_cmdW->enqueueSequence(seq);
    setStatus(QString("Acquiring ADC ch%1 …").arg(ch));
}

void MainWindow::onCmdSequenceDone()
{
    if (m_pendingAcquire) {
        m_pendingAcquire = false;
        setStatus("CMD done — reading data …");
        m_dataW->enqueueRequest(m_pendingDataReq);
    } else if (m_pendingGenerate) {
        m_pendingGenerate = false;
        setStatus("CMD done — uploading waveform …");
        m_dataW->enqueueRequest(m_pendingDataReq);
    }
}

void MainWindow::onGenerateDone()
{
    // Waveform bytes landed in board memory — now fire the playback trigger
    const int ch = m_dacCh;
    setStatus(QString("Waveform uploaded — triggering DAC ch%1").arg(ch));
    m_cmdW->enqueueCommand(
        QString("LocalMemTrigger 1 0 0 0x%1\r\n").arg(1 << ch, 0, 16).toUtf8());
    unlockUi();
}

void MainWindow::onCaptureReady(WaveformData data)
{
    m_adcData = data.samples;
    m_adcP[data.channel].sampleRate = data.sampleRate;
    m_adcPlot->updateWaveform(m_adcData, data.sampleRate);
    setStatus(QString("Capture done — %1 samples, ch%2")
                  .arg(data.samples.size()).arg(data.channel));

    if (m_autoAcquire)
        enqueueAcquireSequence();
    else
        unlockUi();
}

void MainWindow::onSaveAdc()
{
    if (m_adcData.isEmpty()) {
        QMessageBox::information(this, "No data", "Nothing captured yet."); return;
    }
    const QString fname = QFileDialog::getSaveFileName(
        this, "Save ADC data",
        QString("adc%1.bin").arg(m_adcCh),
        "Binary files (*.bin);;All files (*.*)");
    if (fname.isEmpty()) return;

    QFile f(fname);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Error", f.errorString()); return;
    }
    f.write(reinterpret_cast<const char *>(m_adcData.constData()),
            m_adcData.size() * static_cast<int>(sizeof(qint16)));
    setStatus("Saved: " + fname);
}

void MainWindow::onExit()
{
    sendCmd("disconnect\r\n");
    m_cmdW->stop();
    m_dataW->stop();
    m_cmdW->wait(2000);
    m_dataW->wait(2000);
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent *e) { onExit(); e->accept(); }

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::sendCmd(const QString &cmd)
{
    m_cmdW->enqueueCommand(cmd.toUtf8());
}

void MainWindow::lockUi()
{
    m_btnGen->setEnabled(false);
    m_btnLoad->setEnabled(false);
    m_btnAcq->setEnabled(false);
    m_btnSave->setEnabled(false);
}

void MainWindow::unlockUi()
{
    m_btnGen->setEnabled(true);
    m_btnLoad->setEnabled(true);
    m_btnAcq->setEnabled(true);
    m_btnSave->setEnabled(true);
}

void MainWindow::setStatus(const QString &s)
{
    if (m_statusLbl) m_statusLbl->setText(s);
}
