#include "waterfallwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>

// ── Viridis-inspired colour map ───────────────────────────────────────────────
// 0 = cold (dark blue/purple) → 1 = hot (yellow/white)
QRgb WaterfallWidget::dbToColor(double t)
{
    // Clamp
    t = std::max(0.0, std::min(1.0, t));

    // Piecewise linear through: black → deep blue → cyan → green → yellow → white
    struct Stop { double pos; int r,g,b; };
    static const Stop stops[] = {
        {0.00,   8,   0,  64},   // deep purple-black
        {0.15,   0,   0, 180},   // blue
        {0.35,   0, 160, 200},   // cyan
        {0.55,   0, 210,  80},   // green
        {0.75, 230, 230,   0},   // yellow
        {0.90, 255, 120,   0},   // orange
        {1.00, 255, 255, 255},   // white hot
    };
    constexpr int NS = sizeof(stops)/sizeof(stops[0]);

    // Find surrounding stops
    int lo = 0;
    for (int i=1; i<NS; ++i) {
        if (stops[i].pos >= t) { lo = i-1; break; }
        lo = i;
    }
    int hi = std::min(lo+1, NS-1);

    double span = stops[hi].pos - stops[lo].pos;
    double frac = (span > 0) ? (t - stops[lo].pos) / span : 0.0;

    int r = (int)(stops[lo].r + frac*(stops[hi].r - stops[lo].r));
    int g = (int)(stops[lo].g + frac*(stops[hi].g - stops[lo].g));
    int b = (int)(stops[lo].b + frac*(stops[hi].b - stops[lo].b));

    r = std::max(0, std::min(255, r));
    g = std::max(0, std::min(255, g));
    b = std::max(0, std::min(255, b));

    return qRgb(r, g, b);
}

// ─────────────────────────────────────────────────────────────────────────────
WaterfallWidget::WaterfallWidget(const QString &label, QWidget *parent)
    : QWidget(parent), m_label(label)
{
    setMinimumSize(400, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

// ─────────────────────────────────────────────────────────────────────────────
void WaterfallWidget::addRow(const QVector<double> &dbfsSpectrum,
                              double freqMinMHz, double freqMaxMHz)
{
    m_freqMin = freqMinMHz;
    m_freqMax = freqMaxMHz;

    m_rows.prepend(dbfsSpectrum);   // newest row at top
    while (m_rows.size() > m_maxRows)
        m_rows.removeLast();

    m_dirty = true;
    update();
}

void WaterfallWidget::clearHistory()
{
    m_rows.clear();
    m_image = QImage();
    m_dirty = false;
    update();
}

void WaterfallWidget::setDbRange(double minDb, double maxDb)
{
    m_dbMin = minDb;
    m_dbMax = maxDb;
    m_dirty = true;
    update();
}

void WaterfallWidget::setMaxRows(int rows)
{
    m_maxRows = std::max(10, rows);
    while (m_rows.size() > m_maxRows) m_rows.removeLast();
    m_dirty = true;
    update();
}

// ── Image builder ─────────────────────────────────────────────────────────────
void WaterfallWidget::rebuildImage()
{
    if (m_rows.isEmpty()) { m_image = QImage(); return; }

    // Plot area
    QRect wr    = rect();
    int plotW   = wr.width()  - PAD_L - PAD_R;
    int plotH   = wr.height() - PAD_T - PAD_B;
    if (plotW <= 0 || plotH <= 0) { m_image = QImage(); return; }

    int nRows   = m_rows.size();
    int nFreqs  = m_rows[0].size();

    m_image = QImage(plotW, plotH, QImage::Format_RGB32);
    m_image.fill(qRgb(8, 0, 64));

    double dbRange = m_dbMax - m_dbMin;
    if (dbRange == 0) dbRange = 1;

    for (int row = 0; row < plotH; ++row) {
        // Map pixel row → data row (newest = top)
        int dataRow = (int)((double)row / plotH * nRows);
        dataRow = std::max(0, std::min(nRows-1, dataRow));
        const QVector<double> &spec = m_rows[dataRow];

        QRgb *line = reinterpret_cast<QRgb *>(m_image.scanLine(row));
        for (int col = 0; col < plotW; ++col) {
            // Map pixel col → frequency bin
            int bin = (int)((double)col / plotW * nFreqs);
            bin = std::max(0, std::min(nFreqs-1, bin));
            double db = spec[bin];
            double t  = (db - m_dbMin) / dbRange;
            line[col] = dbToColor(t);
        }
    }
    m_dirty = false;
}

// ─────────────────────────────────────────────────────────────────────────────
void WaterfallWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect wr   = rect();
    QRect plot = QRect(wr.left()+PAD_L, wr.top()+PAD_T,
                       wr.width()-PAD_L-PAD_R, wr.height()-PAD_T-PAD_B);

    p.fillRect(wr, QColor(COL_BG));

    if (plot.width() <= 0 || plot.height() <= 0) return;

    // ── Title ─────────────────────────────────────────────────────────────
    QString title = m_label + "  \u2013  Waterfall  (" +
                    QString::number(m_rows.size()) + " frames)";
    p.setPen(QColor(COL_TITLE));
    QFont tf = p.font(); tf.setPixelSize(12); tf.setBold(true); p.setFont(tf);
    p.drawText(QRect(wr.left(), wr.top(), wr.width()-BTN_SZ-8, PAD_T),
               Qt::AlignHCenter|Qt::AlignVCenter, title);

    // ── Maximize button ───────────────────────────────────────────────────
    QRect btn = QRect(wr.right()-BTN_SZ-4, wr.top()+(PAD_T-BTN_SZ)/2, BTN_SZ, BTN_SZ);
    bool hovered = btn.contains(m_mousePos) && m_mouseIn;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(hovered ? COL_BTNHOV : COL_BTN));
    p.drawRoundedRect(btn, 3, 3);
    p.setPen(QPen(QColor(COL_AXIS), 1.5)); p.setBrush(Qt::NoBrush);
    int bm=4; QRect bi=btn.adjusted(bm,bm,-bm,-bm);
    if (m_maximized) {
        p.drawLine(bi.left(),bi.top(),bi.left()+4,bi.top());
        p.drawLine(bi.left(),bi.top(),bi.left(),bi.top()+4);
        p.drawLine(bi.right(),bi.bottom(),bi.right()-4,bi.bottom());
        p.drawLine(bi.right(),bi.bottom(),bi.right(),bi.bottom()-4);
        p.drawLine(bi.left()+1,bi.top()+1,bi.right()-1,bi.bottom()-1);
    } else {
        p.drawLine(bi.left(),bi.top(),bi.left()+4,bi.top());
        p.drawLine(bi.left(),bi.top(),bi.left(),bi.top()+4);
        p.drawLine(bi.right(),bi.bottom(),bi.right()-4,bi.bottom());
        p.drawLine(bi.right(),bi.bottom(),bi.right(),bi.bottom()-4);
        p.drawLine(bi.right(),bi.top(),bi.right()-4,bi.top());
        p.drawLine(bi.right(),bi.top(),bi.right(),bi.top()+4);
        p.drawLine(bi.left(),bi.bottom(),bi.left()+4,bi.bottom());
        p.drawLine(bi.left(),bi.bottom(),bi.left(),bi.bottom()-4);
    }

    // ── Spectrogram image ─────────────────────────────────────────────────
    if (m_dirty || m_image.isNull()) rebuildImage();

    if (!m_image.isNull())
        p.drawImage(plot, m_image);
    else {
        p.fillRect(plot, QColor(8, 0, 64));
        p.setPen(QColor(COL_AXIS));
        QFont ef=p.font(); ef.setPixelSize(11); ef.setBold(false); p.setFont(ef);
        p.drawText(plot, Qt::AlignCenter,
                   "No data yet\nAcquire or Generate to start waterfall");
    }

    // ── Colour scale bar (right side) ─────────────────────────────────────
    int scaleX = plot.right() + 8;
    int scaleW = 16;
    int scaleH = plot.height();
    for (int y = 0; y < scaleH; ++y) {
        double t = 1.0 - (double)y / scaleH;
        p.setPen(QColor(dbToColor(t)));
        p.drawLine(scaleX, plot.top()+y, scaleX+scaleW, plot.top()+y);
    }
    p.setPen(QPen(QColor(COL_AXIS), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(scaleX, plot.top(), scaleW, scaleH);

    // Scale labels
    QFont sf=p.font(); sf.setPixelSize(8); sf.setBold(false); p.setFont(sf);
    p.setPen(QColor(COL_AXIS));
    const int NDB = 5;
    for (int k=0; k<=NDB; ++k) {
        double db  = m_dbMax - k*(m_dbMax-m_dbMin)/NDB;
        int    sy  = plot.top() + k*scaleH/NDB;
        p.drawText(QRect(scaleX+scaleW+2, sy-6, 50, 12),
                   Qt::AlignLeft|Qt::AlignVCenter,
                   QString::number((int)db)+" dB");
    }

    // ── Grid lines ────────────────────────────────────────────────────────
    p.setClipRect(plot);
    const int NX=5, NY=4;
    p.setPen(QPen(QColor(COL_GRID), 1, Qt::DotLine));
    for (int i=0;i<=NX;++i){
        int x=plot.left()+i*plot.width()/NX;
        p.drawLine(x,plot.top(),x,plot.bottom());
    }
    for (int j=0;j<=NY;++j){
        int y=plot.top()+j*plot.height()/NY;
        p.drawLine(plot.left(),y,plot.right(),y);
    }
    p.setClipping(false);

    // ── X axis (frequency) ────────────────────────────────────────────────
    QFont af=p.font(); af.setPixelSize(9); p.setFont(af);
    p.setPen(QColor(COL_AXIS));
    for (int i=0;i<=NX;++i){
        double freq = m_freqMin + i*(m_freqMax-m_freqMin)/NX;
        int x = plot.left()+i*plot.width()/NX;
        p.drawText(QRect(x-24,plot.bottom()+2,48,14),
                   Qt::AlignHCenter, QString::number(freq,'f',0));
    }
    p.drawText(QRect(plot.left(),plot.bottom()+18,plot.width(),14),
               Qt::AlignHCenter, "Freq (MHz)");

    // ── Y axis (time — frames) ────────────────────────────────────────────
    for (int j=0;j<=NY;++j){
        int frames = (int)((double)j/NY * m_rows.size());
        int y = plot.top()+j*plot.height()/NY;
        p.drawText(QRect(0,y-9,PAD_L-3,18),
                   Qt::AlignRight|Qt::AlignVCenter, QString::number(frames));
    }
    p.save();
    p.translate(12, plot.top()+plot.height()/2);
    p.rotate(-90);
    p.drawText(QRect(-35,-6,70,13), Qt::AlignHCenter, "Frames");
    p.restore();

    // ── Crosshair ─────────────────────────────────────────────────────────
    if (m_mouseIn && plot.contains(m_mousePos)) {
        double freqRange = m_freqMax - m_freqMin;
        double dataFreq  = m_freqMin + (double)(m_mousePos.x()-plot.left())/plot.width()*freqRange;
        int    frameIdx  = (int)((double)(m_mousePos.y()-plot.top())/plot.height()*m_rows.size());
        frameIdx = std::max(0, std::min((int)m_rows.size()-1, frameIdx));

        // Sample dBFS at cursor
        double curDb = 0.0;
        if (!m_rows.isEmpty() && !m_rows[frameIdx].isEmpty()) {
            int bin = (int)((double)(m_mousePos.x()-plot.left())/plot.width()*m_rows[frameIdx].size());
            bin = std::max(0, std::min((int)m_rows[frameIdx].size()-1, bin));
            curDb = m_rows[frameIdx][bin];
        }

        p.setPen(QPen(QColor(COL_CROSS), 1, Qt::DashLine));
        p.drawLine(m_mousePos.x(), plot.top(),    m_mousePos.x(), plot.bottom());
        p.drawLine(plot.left(),    m_mousePos.y(), plot.right(),   m_mousePos.y());

        QString tip = QString("Freq: %1 MHz   Frame: %2   %3 dBFS")
                          .arg(dataFreq, 0,'f',1)
                          .arg(frameIdx)
                          .arg(curDb,   0,'f',1);
        QFont rf=p.font(); rf.setPixelSize(10); rf.setBold(false); p.setFont(rf);
        QFontMetrics fm(rf);
        int rw=fm.horizontalAdvance(tip)+10, rh=fm.height()+6;
        int rx=m_mousePos.x()+12, ry=m_mousePos.y()-rh-4;
        if (rx+rw>plot.right())  rx=m_mousePos.x()-rw-4;
        if (ry<plot.top())       ry=m_mousePos.y()+8;
        p.setPen(Qt::NoPen); p.setBrush(QColor(30,30,46,210));
        p.drawRoundedRect(rx,ry,rw,rh,3,3);
        p.setPen(QColor(COL_AXIS));
        p.drawText(rx+5, ry+fm.ascent()+3, tip);
    }

    // ── Border ────────────────────────────────────────────────────────────
    p.setPen(QPen(QColor(COL_AXIS), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);
}

// ─────────────────────────────────────────────────────────────────────────────
void WaterfallWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePos = event->pos();
    m_mouseIn  = true;
    update();
}

void WaterfallWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    QRect wr  = rect();
    QRect btn = QRect(wr.right()-BTN_SZ-4, wr.top()+(PAD_T-BTN_SZ)/2, BTN_SZ, BTN_SZ);
    if (btn.contains(event->pos())) {
        m_maximized = !m_maximized;
        emit maximizeToggled(m_maximized);
        update();
    }
}

void WaterfallWidget::leaveEvent(QEvent *) { m_mouseIn = false; update(); }
