#include "mainwindow.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QScrollArea>
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
    for (auto &p : m_dacP) { p.sampleRate = Constant::DAC_FS;  p.sampleNum = Constant::DAC_SAMPLE_NUM; }
    for (auto &p : m_adcP) { p.sampleRate = Constant::ADC_FS;  p.sampleNum = Constant::ADC_SAMPLE_NUM; }

    connectWorkers(QString(Constant::BOARD_IP));

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);
    root->addWidget(buildMenuPanel());

    // DAC column
    auto *dacCol = new QWidget(central);
    auto *dacColLay = new QVBoxLayout(dacCol);
    dacColLay->setContentsMargins(0,0,0,0); dacColLay->setSpacing(4);
    m_dacPlot = new PlotWidget("DAC", dacCol);
    m_dacWaterfall = new WaterfallWidget("DAC", dacCol);
    m_dacWaterfall->setVisible(false);
    dacColLay->addWidget(m_dacPlot, 3);
    dacColLay->addWidget(m_dacWaterfall, 1);
    root->addWidget(dacCol, 1);

    // ADC column
    auto *adcCol = new QWidget(central);
    auto *adcColLay = new QVBoxLayout(adcCol);
    adcColLay->setContentsMargins(0,0,0,0); adcColLay->setSpacing(4);
    m_adcPlot = new PlotWidget("ADC", adcCol);
    m_adcWaterfall = new WaterfallWidget("ADC", adcCol);
    m_adcWaterfall->setVisible(false);
    adcColLay->addWidget(m_adcPlot, 3);
    adcColLay->addWidget(m_adcWaterfall, 1);
    root->addWidget(adcCol, 1);

    connect(m_dacWaterfall, &WaterfallWidget::maximizeToggled, this, [this, dacColLay](bool max){
        m_dacPlot->setVisible(!max);
        dacColLay->setStretch(0, max ? 0 : 3);
        dacColLay->setStretch(1, 1);
    });
    connect(m_adcWaterfall, &WaterfallWidget::maximizeToggled, this, [this, adcColLay](bool max){
        m_adcPlot->setVisible(!max);
        adcColLay->setStretch(0, max ? 0 : 3);
        adcColLay->setStretch(1, 1);
    });

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
    m_cmdW->stop();  m_dataW->stop();
    m_cmdW->wait(2000); m_dataW->wait(2000);
}

// ─────────────────────────────────────────────────────────────────────────────
// connectWorkers
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::connectWorkers(const QString &ip)
{
    if (m_cmdW)  { m_cmdW->stop();  m_cmdW->wait(3000);  delete m_cmdW;  m_cmdW  = nullptr; }
    if (m_dataW) { m_dataW->stop(); m_dataW->wait(3000); delete m_dataW; m_dataW = nullptr; }

    m_pendingAcquire  = false;
    m_pendingGenerate = false;

    m_cmdW  = new CmdWorker (ip, Constant::CMD_PORT,  this);
    m_dataW = new DataWorker(ip, Constant::DATA_PORT, this);

    connect(m_dataW, &DataWorker::captureReady, this, &MainWindow::onCaptureReady);
    connect(m_dataW, &DataWorker::sendDone,     this, &MainWindow::onGenerateDone);
    connect(m_dataW, &DataWorker::errorOccurred,
            this, [this](const QString &e){ setStatus("DATA ERROR: " + e); unlockUi(); });
    connect(m_cmdW, &CmdWorker::sequenceDone,   this, &MainWindow::onCmdSequenceDone);
    connect(m_cmdW, &CmdWorker::connected,
            this, [this, ip](){ setStatus("Connected to " + ip); });
    connect(m_cmdW, &CmdWorker::errorOccurred,
            this, [this](const QString &e){ setStatus("CMD ERROR: " + e); });

    m_cmdW->start();
    m_dataW->start();
}

// ─────────────────────────────────────────────────────────────────────────────
// Menu panel  (scrollable)
// ─────────────────────────────────────────────────────────────────────────────

QWidget *MainWindow::buildMenuPanel()
{
    // Outer fixed-width container
    auto *outer = new QWidget;
    outer->setFixedWidth(270);
    outer->setStyleSheet("QWidget{background:#181825;}");
    auto *outerLay = new QVBoxLayout(outer);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->setSpacing(0);

    // Scroll area
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet(
        "QScrollArea{background:#181825;border:none;}"
        "QScrollBar:vertical{background:#181825;width:6px;border-radius:3px;margin:0;}"
        "QScrollBar::handle:vertical{background:#45475a;border-radius:3px;min-height:20px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:none;}");
    outerLay->addWidget(scroll);

    // Inner content widget
    auto *panel = new QWidget;
    panel->setStyleSheet("QWidget{background:#181825;}");
    scroll->setWidget(panel);

    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(5);

    // Title
    auto *title = new QLabel("RFSoC Tool");
    title->setStyleSheet("color:#89b4fa;font-size:16px;font-weight:bold;");
    title->setAlignment(Qt::AlignHCenter);
    lay->addWidget(title);
    lay->addWidget(makeSep());

    // ── Connection ────────────────────────────────────────────────────────
    {
        auto *hdr = new QLabel("── Connection ──");
        hdr->setStyleSheet("color:#cba6f7;font-weight:bold;");
        hdr->setAlignment(Qt::AlignHCenter);
        lay->addWidget(hdr);

        lay->addWidget(makeLabel("Board IP"));
        m_ipEdit = makeField(Constant::BOARD_IP);
        lay->addWidget(m_ipEdit);

        auto *btnConnect = new QPushButton("Reconnect"); styleGreen(btnConnect);
        connect(btnConnect, &QPushButton::clicked, this, [this](){
            QString ip = m_ipEdit->text().trimmed();
            if (ip.isEmpty()) { setStatus("Error: IP address is empty"); return; }
            setStatus("Reconnecting to " + ip + " …");
            lockUi();
            connectWorkers(ip);
            unlockUi();
        });
        lay->addWidget(btnConnect);
    }
    lay->addWidget(makeSep());

    // ── DAC ───────────────────────────────────────────────────────────────
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
        m_dacRateEdit = makeField(QString::number(m_dacP[0].sampleRate / 1e9, 'f', 4), true);
        lay->addWidget(m_dacRateEdit);

        lay->addWidget(makeLabel("Waveform Shape"));
        m_comboWaveShape = new QComboBox;
        m_comboWaveShape->addItems({"Sine", "Square"});
        m_comboWaveShape->setStyleSheet(COMBO_STYLE);
        connect(m_comboWaveShape, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx){ m_dacDutyEdit->setEnabled(idx == 1); });
        lay->addWidget(m_comboWaveShape);

        lay->addWidget(makeLabel("Duty Cycle (square only)"));
        m_dacDutyEdit = makeField("0.5");
        m_dacDutyEdit->setEnabled(false);
        lay->addWidget(m_dacDutyEdit);

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

    // ── ADC ───────────────────────────────────────────────────────────────
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
        m_adcRateEdit = makeField(QString::number(m_adcP[0].sampleRate / 1e9, 'f', 4), true);
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

    // ── Waterfall ─────────────────────────────────────────────────────────
    {
        auto *hdr = new QLabel("── Waterfall ──");
        hdr->setStyleSheet("color:#cba6f7;font-weight:bold;");
        hdr->setAlignment(Qt::AlignHCenter);
        lay->addWidget(hdr);

        auto *chkWf = new QCheckBox("Enable Waterfall");
        chkWf->setStyleSheet("QCheckBox{color:#cdd6f4;}");
        connect(chkWf, &QCheckBox::toggled, this, [this](bool v){
            m_waterfallEnabled = v;
            m_dacWaterfall->setVisible(v);
            m_adcWaterfall->setVisible(v);
            if (!v) { m_dacWaterfall->clearHistory(); m_adcWaterfall->clearHistory(); }
        });
        lay->addWidget(chkWf);

        lay->addWidget(makeLabel("dBFS min"));
        auto *wfMin = makeField("-120");
        lay->addWidget(wfMin);
        lay->addWidget(makeLabel("dBFS max"));
        auto *wfMax = makeField("0");
        lay->addWidget(wfMax);

        auto *btnWfRange = new QPushButton("Set Range"); styleGreen(btnWfRange);
        connect(btnWfRange, &QPushButton::clicked, this, [this, wfMin, wfMax](){
            bool ok1, ok2;
            double mn = wfMin->text().toDouble(&ok1);
            double mx = wfMax->text().toDouble(&ok2);
            if (ok1 && ok2 && mn < mx) {
                m_dacWaterfall->setDbRange(mn, mx);
                m_adcWaterfall->setDbRange(mn, mx);
            }
        });
        lay->addWidget(btnWfRange);

        auto *btnWfClear = new QPushButton("Clear History"); styleGreen(btnWfClear);
        connect(btnWfClear, &QPushButton::clicked, this, [this](){
            m_dacWaterfall->clearHistory();
            m_adcWaterfall->clearHistory();
        });
        lay->addWidget(btnWfClear);
    }
    lay->addWidget(makeSep());

    // ── Persistence ───────────────────────────────────────────────────────
    {
        auto *hdr = new QLabel("── Persistence ──");
        hdr->setStyleSheet("color:#cba6f7;font-weight:bold;");
        hdr->setAlignment(Qt::AlignHCenter);
        lay->addWidget(hdr);

        auto *chkP = new QCheckBox("Enable Persistence");
        chkP->setStyleSheet("QCheckBox{color:#cdd6f4;}");
        connect(chkP, &QCheckBox::toggled, this, [this](bool v){
            m_dacPlot->setPersistenceEnabled(v);
            m_adcPlot->setPersistenceEnabled(v);
        });
        lay->addWidget(chkP);

        auto *btnClearP = new QPushButton("Clear"); styleGreen(btnClearP);
        connect(btnClearP, &QPushButton::clicked, this, [this](){
            m_dacPlot->clearPersistence();
            m_adcPlot->clearPersistence();
        });
        lay->addWidget(btnClearP);
    }
    lay->addWidget(makeSep());

    // ── Phase Noise ───────────────────────────────────────────────────────
    {
        auto *hdr = new QLabel("── Phase Noise ──");
        hdr->setStyleSheet("color:#cba6f7;font-weight:bold;");
        hdr->setAlignment(Qt::AlignHCenter);
        lay->addWidget(hdr);

        auto *chkPN = new QCheckBox("Enable Phase Noise");
        chkPN->setStyleSheet("QCheckBox{color:#cdd6f4;}");
        connect(chkPN, &QCheckBox::toggled, this, [this](bool v){
            m_dacPlot->setPhaseNoiseEnabled(v);
            m_adcPlot->setPhaseNoiseEnabled(v);
        });
        lay->addWidget(chkPN);
    }
    lay->addWidget(makeSep());

    // ── Export ────────────────────────────────────────────────────────────
    {
        auto *hdr = new QLabel("── Export ──");
        hdr->setStyleSheet("color:#cba6f7;font-weight:bold;");
        hdr->setAlignment(Qt::AlignHCenter);
        lay->addWidget(hdr);

        auto *rowE = new QHBoxLayout;
        auto *btnPng = new QPushButton("PNG"); styleGreen(btnPng);
        auto *btnCsv = new QPushButton("CSV"); styleGreen(btnCsv);

        connect(btnPng, &QPushButton::clicked, this, [this](){
            QString fname = QFileDialog::getSaveFileName(
                this, "Export PNG", "rfsoc_plot.png", "PNG Image (*.png)");
            if (fname.isEmpty()) return;
            QString df = fname; df.replace(".png", "_dac.png");
            QString af = fname; af.replace(".png", "_adc.png");
            m_dacPlot->exportPng(df);
            m_adcPlot->exportPng(af);
            setStatus("PNG saved: " + df + ", " + af);
        });

        connect(btnCsv, &QPushButton::clicked, this, [this](){
            QString fname = QFileDialog::getSaveFileName(
                this, "Export CSV", "rfsoc_data.csv", "CSV File (*.csv)");
            if (fname.isEmpty()) return;
            QString df = fname; df.replace(".csv", "_dac.csv");
            QString af = fname; af.replace(".csv", "_adc.csv");
            m_dacPlot->exportCsv(df);
            m_adcPlot->exportCsv(af);
            setStatus("CSV saved: " + df + ", " + af);
        });

        rowE->addWidget(btnPng);
        rowE->addWidget(btnCsv);
        lay->addLayout(rowE);
    }
    lay->addWidget(makeSep());

    // ── Exit ──────────────────────────────────────────────────────────────
    auto *btnExit = new QPushButton("Exit"); styleRed(btnExit);
    connect(btnExit, &QPushButton::clicked, this, &MainWindow::onExit);
    lay->addWidget(btnExit);
    lay->addStretch();

    return outer;   // return the scroll container, not the inner panel
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
    bool isSquare = (m_comboWaveShape->currentIndex() == 1);
    double duty = 0.5;
    if (isSquare) {
        bool dcOk = false;
        duty = m_dacDutyEdit->text().toDouble(&dcOk);
        if (!dcOk || duty <= 0.0 || duty >= 1.0) duty = 0.5;
        m_dacData = WaveformGenerator::generateSquareNormalized(
            p.freq, p.theta, p.sampleRate, p.sampleNum, trimmed, duty);
    } else {
        m_dacData = WaveformGenerator::generateToneNormalized(
            p.freq, p.theta, p.sampleRate, p.sampleNum, trimmed);
    }
    p.sampleNumTrimmed = trimmed;
    m_dacPlot->setSquareMode(isSquare, duty);
    m_adcPlot->setSquareMode(isSquare, duty);
    m_dacPlot->updateWaveform(m_dacData, p.sampleRate);

    const int ch = m_dacCh;
    m_pendingDataReq.type    = DataRequest::SendWaveform;
    m_pendingDataReq.command = QString("WriteDataToMemoryFromSocket 3 %1 %2 %3 0\r\n")
                                   .arg(ch/4).arg(ch%4).arg(p.sampleNum * 2).toUtf8();
    m_pendingDataReq.payload = QByteArray(
        reinterpret_cast<const char *>(m_dacData.constData()),
        m_dacData.size() * static_cast<int>(sizeof(qint16)));
    m_pendingGenerate = true;

    lockUi();

    QList<CmdItem> seq;
    seq << CmdItem{ "TermMode 0\r\n",                                          100, false }
        << CmdItem{ "LocalMemInfo 1\r\n",                                      100, false }
        << CmdItem{ "LocalMemTrigger 1 0 0 0x0\r\n",                          100, false }
        << CmdItem{ QString("SetLocalMemSample 1 %1 %2 %3\r\n")
                        .arg(ch/4).arg(ch%4).arg(p.sampleNumTrimmed).toUtf8(), 100, true  };
    m_cmdW->enqueueSequence(seq);
    setStatus(QString("Preparing DAC ch%1 …").arg(ch));
}

void MainWindow::onLoadDac()
{
    const QString fname = QFileDialog::getOpenFileName(
        this, "Load DAC binary", {}, "Binary files (*.bin);;All files (*.*)");
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

    m_pendingDataReq.type          = DataRequest::ReceiveCapture;
    m_pendingDataReq.command       = QString("ReadDataFromMemoryToSocket 3 %1 %2 %3 0\r\n")
                                         .arg(ch/4).arg(ch%4).arg(p.sampleNum * 2).toUtf8();
    m_pendingDataReq.expectedBytes = p.sampleNum * 2;
    m_pendingDataReq.sampleRate    = p.sampleRate;
    m_pendingDataReq.channel       = ch;
    m_pendingAcquire = true;

    QList<CmdItem> seq;
    seq << CmdItem{ "TermMode 0\r\n",                                              150, false }
        << CmdItem{ QString("SetLocalMemSample 0 %1 %2 %3\r\n")
                        .arg(ch/4).arg(ch%4).arg(p.sampleNum).toUtf8(),            150, false }
        << CmdItem{ "LocalMemInfo 0\r\n",                                          150, false }
        << CmdItem{ QString("LocalMemTrigger 0 0 %1 0x%2\r\n")
                        .arg(p.sampleNum).arg(1 << ch, 0, 16).toUtf8(),            150, true  };
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
    const int ch = m_dacCh;
    setStatus(QString("Waveform uploaded — triggering DAC ch%1").arg(ch));
    m_cmdW->enqueueCommand(
        QString("LocalMemTrigger 1 0 0 0x%1\r\n").arg(1 << ch, 0, 16).toUtf8());
    if (m_waterfallEnabled)
        m_dacWaterfall->addRow(m_dacPlot->lastFreqSpectrum(),
                               0.0, m_dacP[m_dacCh].sampleRate / 2e6);
    unlockUi();
}

void MainWindow::onCaptureReady(WaveformData data)
{
    m_adcData = data.samples;
    m_adcP[data.channel].sampleRate = data.sampleRate;
    m_adcPlot->updateWaveform(m_adcData, data.sampleRate);
    if (m_waterfallEnabled)
        m_adcWaterfall->addRow(m_adcPlot->lastFreqSpectrum(),
                               0.0, data.sampleRate / 2e6);
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
    m_cmdW->stop();  m_dataW->stop();
    m_cmdW->wait(2000); m_dataW->wait(2000);
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
    m_btnGen->setEnabled(false);  m_btnLoad->setEnabled(false);
    m_btnAcq->setEnabled(false);  m_btnSave->setEnabled(false);
}

void MainWindow::unlockUi()
{
    m_btnGen->setEnabled(true);   m_btnLoad->setEnabled(true);
    m_btnAcq->setEnabled(true);   m_btnSave->setEnabled(true);
}

void MainWindow::setStatus(const QString &s)
{
    if (m_statusLbl) m_statusLbl->setText(s);
}
