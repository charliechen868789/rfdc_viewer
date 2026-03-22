#include "plotwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>
#include <complex>
#include <vector>

// ── Radix-2 DIT FFT ──────────────────────────────────────────────────────────
static std::vector<std::complex<double>> doFFT(std::vector<std::complex<double>> x)
{
    int N = (int)x.size();
    if (N <= 1) return x;
    int n2 = 1;
    while (n2 < N) n2 <<= 1;
    x.resize(n2, {0, 0});
    N = n2;
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (int len = 2; len <= N; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < N; i += len) {
            std::complex<double> w(1, 0);
            for (int j = 0; j < len / 2; ++j) {
                auto u = x[i + j], v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    return x;
}

// ── Constructor ───────────────────────────────────────────────────────────────
PlotWidget::PlotWidget(const QString &label, QWidget *parent)
    : QWidget(parent), m_label(label)
{
    setMinimumSize(400, 420);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);

    m_time.title     = label + "  \u2013  Time Domain";
    m_time.xLabel    = "Time (\u00b5s)";
    m_time.yLabel    = "Amplitude";
    m_time.lineColor = QColor(COL_TIME);
    m_time.dataXMin = m_time.viewXMin = 0;
    m_time.dataXMax = m_time.viewXMax = 1;
    m_time.dataYMin = m_time.viewYMin = -32768;
    m_time.dataYMax = m_time.viewYMax =  32767;

    m_freq.title     = label + "  \u2013  Frequency Domain";
    m_freq.xLabel    = "Freq (MHz)";
    m_freq.yLabel    = "dBFS";
    m_freq.lineColor = QColor(COL_FREQ);
    m_freq.dataXMin = m_freq.viewXMin = 0;
    m_freq.dataXMax = m_freq.viewXMax = 1000;
    m_freq.dataYMin = m_freq.viewYMin = -120;
    m_freq.dataYMax = m_freq.viewYMax = 0;
}

// ── Public API ────────────────────────────────────────────────────────────────
void PlotWidget::updateWaveform(const QVector<qint16> &samples, double sampleRateHz)
{
    if (samples.isEmpty()) return;
    buildTimePlot(samples, sampleRateHz);
    buildFreqPlot(samples, sampleRateHz);
    update();
}

// ── Data builders ─────────────────────────────────────────────────────────────
void PlotWidget::buildTimePlot(const QVector<qint16> &s, double rateHz)
{
    int N = s.size();
    m_time.xs.resize(N);
    m_time.ys.resize(N);
    double tScale = 1e6 / rateHz;
    double yMin = 1e9, yMax = -1e9;
    for (int i = 0; i < N; ++i) {
        m_time.xs[i] = i * tScale;
        m_time.ys[i] = s[i];
        if (s[i] < yMin) yMin = s[i];
        if (s[i] > yMax) yMax = s[i];
    }
    double pad = (yMax - yMin) * 0.05;
    m_time.dataXMin = 0;
    m_time.dataXMax = (N - 1) * tScale;
    m_time.dataYMin = yMin - pad;
    m_time.dataYMax = yMax + pad;
    if (m_time.dataYMin == m_time.dataYMax) { m_time.dataYMin -= 1; m_time.dataYMax += 1; }
    // Reset view to full on new data
    m_time.viewXMin = m_time.dataXMin;
    m_time.viewXMax = m_time.dataXMax;
    m_time.viewYMin = m_time.dataYMin;
    m_time.viewYMax = m_time.dataYMax;
    // No peaks on time domain
    m_time.peaks.clear();
}

void PlotWidget::buildFreqPlot(const QVector<qint16> &s, double rateHz)
{
    QVector<double> freqs;
    QVector<double> db = hammingFFT_dBFS(s, rateHz, freqs);
    m_freq.xs = freqs;
    m_freq.ys = db;
    m_freq.dataXMin = 0;
    m_freq.dataXMax = freqs.isEmpty() ? 1.0 : freqs.last();
    double yMin = 1e9, yMax = -1e9;
    for (double v : db) { if (v < yMin) yMin = v; if (v > yMax) yMax = v; }
    m_freq.dataYMin = yMin - 5.0;
    m_freq.dataYMax = yMax + 5.0;
    if (m_freq.dataYMin == m_freq.dataYMax) { m_freq.dataYMin -= 1; m_freq.dataYMax += 1; }
    // Reset view to full on new data
    m_freq.viewXMin = m_freq.dataXMin;
    m_freq.viewXMax = m_freq.dataXMax;
    m_freq.viewYMin = m_freq.dataYMin;
    m_freq.viewYMax = m_freq.dataYMax;
    findPeaks(m_freq);
}

QVector<double> PlotWidget::hammingFFT_dBFS(
    const QVector<qint16> &s, double rateHz,
    QVector<double> &freqsMHz)
{
    int N = s.size();
    constexpr double ref = 32768.0;
    std::vector<std::complex<double>> cx(N);
    double winSum = 0;
    for (int i = 0; i < N; ++i) {
        double w = 0.54 - 0.46 * std::cos(2.0 * M_PI * i / (N - 1));
        cx[i] = {s[i] * w, 0.0};
        winSum += w;
    }
    auto spec = doFFT(cx);
    int half = N / 2 + 1;
    QVector<double> db(half);
    freqsMHz.resize(half);
    double scale = 2.0 / winSum;
    for (int i = 0; i < half; ++i) {
        double mag  = std::abs(spec[i]) * scale;
        db[i]       = 20.0 * std::log10(std::max(mag / ref, 1e-12));
        freqsMHz[i] = (double)i * rateHz / (N * 1e6);
    }
    return db;
}

// ── Peak detection (FFT only) ─────────────────────────────────────────────────
void PlotWidget::findPeaks(PlotData &d)
{
    d.peaks.clear();
    int N = d.ys.size();
    if (N < 3) return;

    const int    MIN_DIST  = 5;
    const double MIN_PROM  = 6.0;   // dB
    const int    MAX_PEAKS = 5;

    struct Candidate { int idx; double val; };
    QVector<Candidate> cands;

    for (int i = MIN_DIST; i < N - MIN_DIST; ++i) {
        bool isMax = true;
        for (int k = i - MIN_DIST; k <= i + MIN_DIST && isMax; ++k)
            if (k != i && d.ys[k] >= d.ys[i]) isMax = false;
        if (!isMax) continue;

        int   lo   = std::max(0,   i - 20);
        int   hi   = std::min(N-1, i + 20);
        double lMin = *std::min_element(d.ys.begin() + lo, d.ys.begin() + i);
        double rMin = *std::min_element(d.ys.begin() + i + 1, d.ys.begin() + hi + 1);
        double prom = d.ys[i] - std::max(lMin, rMin);
        if (prom < MIN_PROM) continue;
        cands.append({i, d.ys[i]});
    }

    std::sort(cands.begin(), cands.end(),
              [](const Candidate &a, const Candidate &b){ return a.val > b.val; });
    if (cands.size() > MAX_PEAKS) cands.resize(MAX_PEAKS);

    for (const auto &c : cands) {
        Peak pk;
        pk.x     = d.xs[c.idx];
        pk.y     = c.val;
        pk.label = QString("%1 MHz\n%2 dBFS")
                       .arg(pk.x, 0, 'f', 2)
                       .arg(pk.y, 0, 'f', 1);
        d.peaks.append(pk);
    }
}

// ── Coordinate helpers ────────────────────────────────────────────────────────
QRect PlotWidget::plotRect(const QRect &wr) const
{
    return QRect(wr.left()  + PAD_L,
                 wr.top()   + PAD_T,
                 wr.width() - PAD_L - PAD_R,
                 wr.height()- PAD_T - PAD_B);
}

bool PlotWidget::hitTimePlot(const QPoint &pos) const
{
    return plotRect(QRect(0, 0, width(), height() / 2)).contains(pos);
}

bool PlotWidget::hitFreqPlot(const QPoint &pos) const
{
    int half = height() / 2;
    return plotRect(QRect(0, half, width(), height() - half)).contains(pos);
}

// ── Mouse events ──────────────────────────────────────────────────────────────
void PlotWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        bool inTime = hitTimePlot(event->pos());
        bool inFreq = hitFreqPlot(event->pos());
        if (inTime || inFreq) {
            m_selecting    = true;
            m_selStart     = event->pos();
            m_selEnd       = event->pos();
            m_selectIsTime = inTime;
            setCursor(Qt::CrossCursor);
        }
    }
}

void PlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePos      = event->pos();
    m_mouseInWidget = true;
    if (m_selecting)
        m_selEnd = event->pos();
    update();
}

void PlotWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_selecting) return;
    m_selecting = false;
    setCursor(Qt::ArrowCursor);

    int half  = height() / 2;
    PlotData &d   = m_selectIsTime ? m_time : m_freq;
    QRect wRect   = m_selectIsTime ? QRect(0, 0, width(), half)
                                 : QRect(0, half, width(), height() - half);
    QRect plot    = plotRect(wRect);

    int x1 = qMin(m_selStart.x(), m_selEnd.x());
    int x2 = qMax(m_selStart.x(), m_selEnd.x());
    int y1 = qMin(m_selStart.y(), m_selEnd.y());
    int y2 = qMax(m_selStart.y(), m_selEnd.y());

    // Ignore tiny drags (< 5 px) — treat as click
    if ((x2 - x1) < 5 || (y2 - y1) < 5) { update(); return; }

    // Clamp selection to plot area
    x1 = qMax(x1, plot.left());
    x2 = qMin(x2, plot.right());
    y1 = qMax(y1, plot.top());
    y2 = qMin(y2, plot.bottom());

    double xRange = d.viewXMax - d.viewXMin;
    double yRange = d.viewYMax - d.viewYMin;

    double newXMin = d.viewXMin + (double)(x1 - plot.left()) / plot.width()  * xRange;
    double newXMax = d.viewXMin + (double)(x2 - plot.left()) / plot.width()  * xRange;
    double newYMax = d.viewYMax - (double)(y1 - plot.top())  / plot.height() * yRange;
    double newYMin = d.viewYMax - (double)(y2 - plot.top())  / plot.height() * yRange;

    d.viewXMin = newXMin;
    d.viewXMax = newXMax;
    d.viewYMin = newYMin;
    d.viewYMax = newYMax;

    update();
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Reset zoom to full data view
    auto reset = [](PlotData &d) {
        d.viewXMin = d.dataXMin;
        d.viewXMax = d.dataXMax;
        d.viewYMin = d.dataYMin;
        d.viewYMax = d.dataYMax;
    };
    if (hitTimePlot(event->pos()))      reset(m_time);
    else if (hitFreqPlot(event->pos())) reset(m_freq);
    update();
}

void PlotWidget::leaveEvent(QEvent *)
{
    m_mouseInWidget = false;
    update();
}

// ── paintEvent ────────────────────────────────────────────────────────────────
void PlotWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(COL_BG));

    int half = height() / 2;
    drawPlot(p, QRect(0, 0,    width(), half),           m_time);
    drawPlot(p, QRect(0, half, width(), height() - half), m_freq);
}

// ── drawPlot ──────────────────────────────────────────────────────────────────
void PlotWidget::drawPlot(QPainter &p, const QRect &wr, PlotData &d) const
{
    QRect plot = plotRect(wr);
    if (plot.width() <= 0 || plot.height() <= 0) return;

    p.fillRect(wr,   QColor(COL_BG));
    p.fillRect(plot, QColor(COL_PLOTBG));

    // ── title ─────────────────────────────────────────────────────────────
    QString titleStr = d.title;
    bool zoomed = (d.viewXMin > d.dataXMin + 1e-9 ||
                   d.viewXMax < d.dataXMax - 1e-9 ||
                   d.viewYMin > d.dataYMin + 1e-9 ||
                   d.viewYMax < d.dataYMax - 1e-9);
    if (zoomed)
        titleStr += "   [dbl-click to reset]";

    p.setPen(QColor(COL_TITLE));
    QFont tf = p.font(); tf.setPixelSize(12); tf.setBold(true); p.setFont(tf);
    p.drawText(QRect(wr.left(), wr.top(), wr.width(), PAD_T),
               Qt::AlignHCenter | Qt::AlignVCenter, titleStr);

    // ── grid ──────────────────────────────────────────────────────────────
    const int NX = 5, NY = 4;
    p.setPen(QPen(QColor(COL_GRID), 1, Qt::DotLine));
    for (int i = 0; i <= NX; ++i) {
        int x = plot.left() + i * plot.width() / NX;
        p.drawLine(x, plot.top(), x, plot.bottom());
    }
    for (int j = 0; j <= NY; ++j) {
        int y = plot.top() + j * plot.height() / NY;
        p.drawLine(plot.left(), y, plot.right(), y);
    }

    // ── axis tick labels ──────────────────────────────────────────────────
    QFont af = p.font(); af.setPixelSize(9); af.setBold(false); p.setFont(af);
    p.setPen(QColor(COL_AXIS));

    for (int j = 0; j <= NY; ++j) {
        double val = d.viewYMax - j * (d.viewYMax - d.viewYMin) / NY;
        int    y   = plot.top() + j * plot.height() / NY;
        p.drawText(QRect(wr.left(), y - 9, PAD_L - 3, 18),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(val, 'f', 0));
    }
    for (int i = 0; i <= NX; ++i) {
        double val = d.viewXMin + i * (d.viewXMax - d.viewXMin) / NX;
        int    x   = plot.left() + i * plot.width() / NX;
        p.drawText(QRect(x - 24, plot.bottom() + 2, 48, 14),
                   Qt::AlignHCenter, QString::number(val, 'f', 1));
    }
    p.drawText(QRect(plot.left(), plot.bottom() + 18, plot.width(), 14),
               Qt::AlignHCenter, d.xLabel);

    // Y-axis label (rotated)
    p.save();
    p.translate(wr.left() + 11, plot.top() + plot.height() / 2);
    p.rotate(-90);
    p.drawText(QRect(-40, -6, 80, 13), Qt::AlignHCenter, d.yLabel);
    p.restore();

    // ── waveform ──────────────────────────────────────────────────────────
    if (d.xs.isEmpty()) return;

    double xRange = d.viewXMax - d.viewXMin; if (xRange == 0) xRange = 1;
    double yRange = d.viewYMax - d.viewYMin; if (yRange == 0) yRange = 1;

    // Find visible sample range via linear scan
    int N = d.xs.size();
    int iStart = 0, iEnd = N - 1;
    for (int i = 0;     i < N;  ++i) { if (d.xs[i] >= d.viewXMin) { iStart = i; break; } }
    for (int i = N - 1; i >= 0; --i) { if (d.xs[i] <= d.viewXMax) { iEnd   = i; break; } }

    int visN = iEnd - iStart + 1;
    int step = std::max(1, visN / plot.width());

    p.setPen(QPen(d.lineColor, 1.2f));
    p.setClipRect(plot);

    QPainterPath path;
    bool first = true;
    for (int i = iStart; i <= iEnd; i += step) {
        double px = plot.left()   + (d.xs[i] - d.viewXMin) / xRange * plot.width();
        double py = plot.bottom() - (d.ys[i] - d.viewYMin) / yRange * plot.height();
        if (first) { path.moveTo(px, py); first = false; }
        else          path.lineTo(px, py);
    }
    p.drawPath(path);

    // ── peaks (FFT only, time has none) ───────────────────────────────────
    drawPeaks(p, plot, d);

    // ── rubber-band selection ─────────────────────────────────────────────
    drawRubberBand(p, plot, d);

    p.setClipping(false);

    // ── crosshair ─────────────────────────────────────────────────────────
    drawCrosshair(p, plot, d);

    // ── border ────────────────────────────────────────────────────────────
    p.setPen(QPen(QColor(COL_AXIS), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);
}

// ── Peak markers ─────────────────────────────────────────────────────────────
void PlotWidget::drawPeaks(QPainter &p, const QRect &plot, const PlotData &d) const
{
    if (d.peaks.isEmpty()) return;

    double xRange = d.viewXMax - d.viewXMin; if (xRange == 0) xRange = 1;
    double yRange = d.viewYMax - d.viewYMin; if (yRange == 0) yRange = 1;

    QFont pf = p.font(); pf.setPixelSize(9); pf.setBold(true); p.setFont(pf);
    QFontMetrics fm(pf);

    for (const Peak &pk : d.peaks) {
        if (pk.x < d.viewXMin || pk.x > d.viewXMax) continue;
        if (pk.y < d.viewYMin || pk.y > d.viewYMax) continue;

        int px = plot.left()   + (int)((pk.x - d.viewXMin) / xRange * plot.width());
        int py = plot.bottom() - (int)((pk.y - d.viewYMin) / yRange * plot.height());

        // Diamond marker
        p.setPen(QPen(QColor(COL_PEAK), 1.5));
        p.setBrush(QColor(COL_PEAK));
        const int D = 5;
        QPolygon diamond;
        diamond << QPoint(px,     py - D)
                << QPoint(px + D, py)
                << QPoint(px,     py + D)
                << QPoint(px - D, py);
        p.drawPolygon(diamond);

        // Dashed vertical drop line to x-axis
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(COL_PEAK), 1, Qt::DashLine));
        p.drawLine(px, py + D, px, plot.bottom());

        // Label box
        QStringList lines = pk.label.split('\n');
        int boxW = 0, boxH = 4;
        for (const QString &ln : lines) {
            boxW  = std::max(boxW, fm.horizontalAdvance(ln) + 10);
            boxH += fm.height();
        }

        int bx = px - boxW / 2;
        int by = py - D - boxH - 4;
        if (bx < plot.left())         bx = plot.left();
        if (bx + boxW > plot.right()) bx = plot.right() - boxW;
        if (by < plot.top())          by = py + D + 4;

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x88, 0x00, 0x00, 210));
        p.drawRoundedRect(bx, by, boxW, boxH, 3, 3);

        p.setPen(QColor(COL_PEAK));
        p.setBrush(Qt::NoBrush);
        int ly = by + 2 + fm.ascent();
        for (const QString &ln : lines) {
            p.drawText(bx + 5, ly, ln);
            ly += fm.height();
        }
    }
    p.setBrush(Qt::NoBrush);
}

// ── Rubber-band ───────────────────────────────────────────────────────────────
void PlotWidget::drawRubberBand(QPainter &p, const QRect &plot, const PlotData &d) const
{
    if (!m_selecting) return;
    bool isThis = (m_selectIsTime ? (&d == &m_time) : (&d == &m_freq));
    if (!isThis) return;

    int x1 = qMax(qMin(m_selStart.x(), m_selEnd.x()), plot.left());
    int x2 = qMin(qMax(m_selStart.x(), m_selEnd.x()), plot.right());
    int y1 = qMax(qMin(m_selStart.y(), m_selEnd.y()), plot.top());
    int y2 = qMin(qMax(m_selStart.y(), m_selEnd.y()), plot.bottom());

    p.setPen(QPen(Qt::white, 1, Qt::DashLine));
    p.setBrush(QColor(255, 255, 255, 30));
    p.drawRect(x1, y1, x2 - x1, y2 - y1);
}

// ── Crosshair ─────────────────────────────────────────────────────────────────
void PlotWidget::drawCrosshair(QPainter &p, const QRect &plot, const PlotData &d) const
{
    if (!m_mouseInWidget || !plot.contains(m_mousePos)) return;

    double xRange = d.viewXMax - d.viewXMin; if (xRange == 0) xRange = 1;
    double yRange = d.viewYMax - d.viewYMin; if (yRange == 0) yRange = 1;

    double dataX = d.viewXMin + (double)(m_mousePos.x() - plot.left()) / plot.width()  * xRange;
    double dataY = d.viewYMax - (double)(m_mousePos.y() - plot.top())  / plot.height() * yRange;

    // Crosshair lines
    p.setPen(QPen(QColor(COL_CROSS), 1, Qt::DashLine));
    p.drawLine(m_mousePos.x(), plot.top(),    m_mousePos.x(), plot.bottom());
    p.drawLine(plot.left(),    m_mousePos.y(), plot.right(),   m_mousePos.y());

    // Readout label
    QString readout = QString("X: %1  Y: %2").arg(dataX, 0, 'f', 3).arg(dataY, 0, 'f', 1);
    QFont rf = p.font(); rf.setPixelSize(10); rf.setBold(false); p.setFont(rf);
    QFontMetrics fm(rf);
    int rw = fm.horizontalAdvance(readout) + 10;
    int rh = fm.height() + 6;

    int rx = m_mousePos.x() + 12;
    int ry = m_mousePos.y() - rh - 4;
    if (rx + rw > plot.right())  rx = m_mousePos.x() - rw - 4;
    if (ry < plot.top())         ry = m_mousePos.y() + 8;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 46, 210));
    p.drawRoundedRect(rx, ry, rw, rh, 3, 3);
    p.setPen(QColor(COL_AXIS));
    p.drawText(rx + 5, ry + fm.ascent() + 3, readout);
}
