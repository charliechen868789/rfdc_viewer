#pragma once
#include <QWidget>
#include <QVector>
#include <QString>
#include <QPoint>
#include <QRect>
#include <cstdint>

/**
 * PlotWidget
 * ----------
 * Time + Frequency + Phase Noise plots with:
 *   - RF metrics panel (SNR, SFDR, THD, ENOB / square rolloff)
 *   - Harmonic markers
 *   - Persistence / overlay mode (last N FFT captures)
 *   - Phase noise plot (dBc/Hz vs offset)
 *   - Rubber-band zoom, double-click reset
 *   - Crosshair readout
 *   - Export PNG / CSV
 *   - Maximize / restore per sub-plot
 */
class PlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlotWidget(const QString &label, QWidget *parent = nullptr);

    void updateWaveform(const QVector<qint16> &samples, double sampleRateHz);
    void setSquareMode(bool square, double dutyCycle = 0.5);

    // Persistence
    void setPersistenceEnabled(bool on);
    void setPersistenceDepth(int n);   // number of traces to keep
    void clearPersistence();

    // Phase noise
    void setPhaseNoiseEnabled(bool on);

    // Export
    void exportPng(const QString &filePath);
    void exportCsv(const QString &filePath);

    QVector<double> lastFreqSpectrum() const { return m_freq.ys; }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // ── RF metrics ────────────────────────────────────────────────────────
    struct RfMetrics {
        double snr = 0, sfdr = 0, thd = 0, enob = 0;
        double fundFreq = 0, fundPow = 0, bandwidth = 0;
        QVector<double> harmonicFreqs, harmonicPows, idealHarmonicPows;
        bool isSquare = false, valid = false;
    };

    // ── plot data ─────────────────────────────────────────────────────────
    struct Peak { double x, y; QString label; };

    struct PlotData {
        QVector<double> xs, ys;
        double dataXMin=0, dataXMax=1, dataYMin=-1, dataYMax=1;
        double viewXMin=0, viewXMax=1, viewYMin=-1, viewYMax=1;
        QString xLabel, yLabel, title;
        QColor  lineColor;
        QVector<Peak> peaks;
    };

    // ── view state ────────────────────────────────────────────────────────
    enum ViewState { BothPlots, TimeOnly, FreqOnly, PhaseNoiseOnly };
    ViewState m_viewState = BothPlots;

    // ── builders ──────────────────────────────────────────────────────────
    void buildTimePlot(const QVector<qint16> &s, double rateHz);
    void buildFreqPlot(const QVector<qint16> &s, double rateHz);
    void buildPhaseNoise(const QVector<double> &db,
                         const QVector<double> &freqsMHz, double rateHz);
    void findPeaks(PlotData &d);
    void computeMetrics(const QVector<double> &db,
                        const QVector<double> &freqsMHz, double sampleRateHz);

    static QVector<double> hammingFFT_dBFS(
        const QVector<qint16> &s, double rateHz,
        QVector<double> &freqsMHz);

    // ── drawing ───────────────────────────────────────────────────────────
    void drawPlot(QPainter &p, const QRect &wr, PlotData &d) const;
    void drawPersistence(QPainter &p, const QRect &plot) const;
    void drawPhaseNoisePlot(QPainter &p, const QRect &wr) const;
    void drawMetricsPanel(QPainter &p, const QRect &wr) const;
    void drawHarmonicMarkers(QPainter &p, const QRect &plot) const;
    void drawPeaks(QPainter &p, const QRect &plot, const PlotData &d) const;
    void drawCrosshair(QPainter &p, const QRect &plot, const PlotData &d) const;
    void drawRubberBand(QPainter &p, const QRect &plot, const PlotData &d) const;
    void drawMaxButton(QPainter &p, const QRect &wr, bool isMaximized) const;

    // ── layout ────────────────────────────────────────────────────────────
    QRect timeWidgetRect()       const;
    QRect freqWidgetRect()       const;
    QRect phaseNoiseWidgetRect() const;
    QRect plotRect(const QRect &wr) const;
    QRect maxBtnRect(const QRect &wr) const;
    QRect metricsPanelRect(const QRect &freqWr) const;

    bool hitTimePlot(const QPoint &p)      const;
    bool hitFreqPlot(const QPoint &p)      const;
    bool hitPNPlot(const QPoint &p)        const;
    bool hitTimeMaxBtn(const QPoint &p)    const;
    bool hitFreqMaxBtn(const QPoint &p)    const;
    bool hitPNMaxBtn(const QPoint &p)      const;

    // ── state ─────────────────────────────────────────────────────────────
    QString   m_label;
    PlotData  m_time, m_freq;
    RfMetrics m_metrics;

    // waveform type
    bool   m_isSquare  = false;
    double m_dutyCycle = 0.5;

    // persistence
    bool   m_persistEnabled = false;
    int    m_persistDepth   = 20;
    QVector<QVector<double>> m_persistTraces;  // last N freq spectra (ys only)

    // phase noise
    bool            m_phaseNoiseEnabled = false;
    PlotData        m_pn;    // x=offset MHz, y=dBc/Hz

    // rubber-band
    bool   m_selecting    = false;
    QPoint m_selStart, m_selEnd;
    bool   m_selectIsTime = false;

    // crosshair
    QPoint m_mousePos;
    bool   m_mouseInWidget = false;

    static constexpr int PAD_L  = 56, PAD_R = 12, PAD_T = 28, PAD_B = 38;
    static constexpr int BTN_SZ = 20;
    static constexpr int MET_W  = 210;

    static constexpr QRgb COL_BG      = 0xFF1E1E2E;
    static constexpr QRgb COL_PLOTBG  = 0xFF181825;
    static constexpr QRgb COL_GRID    = 0xFF313244;
    static constexpr QRgb COL_AXIS    = 0xFFCDD6F4;
    static constexpr QRgb COL_TITLE   = 0xFFCBA6F7;
    static constexpr QRgb COL_TIME    = 0xFF89B4FA;
    static constexpr QRgb COL_FREQ    = 0xFFA6E3A1;
    static constexpr QRgb COL_PEAK    = 0xFFFF5555;
    static constexpr QRgb COL_CROSS   = 0x66FFFFFF;
    static constexpr QRgb COL_BTN     = 0xFF45475A;
    static constexpr QRgb COL_BTNHOV  = 0xFF6C7086;
    static constexpr QRgb COL_HARM    = 0xFFFFB86C;
    static constexpr QRgb COL_METBG   = 0xCC181825;
    static constexpr QRgb COL_PERSIST = 0x2289B4FA;  // semi-transparent blue
    static constexpr QRgb COL_PN      = 0xFFFF9580;  // phase noise trace
};
