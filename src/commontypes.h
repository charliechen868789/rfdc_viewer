#pragma once
#include <QVector>
#include <QMetaType>

// ── Channel parameter structs ───────────────────────────────────────────────

struct DacParams {
    qint64  freq           = 100'000'000;
    double  sampleRate     = 9824e6;
    int     sampleNum      = 8192;
    int     sampleNumTrimmed = 8192;
    double  theta          = 0.0;
    double  amplitude      = 10000.0;
};

struct AdcParams {
    double  sampleRate = 2456e6;
    int     sampleNum  = 8192;
};

// ── Waveform data ────────────────────────────────────────────────────────────

struct WaveformData {
    QVector<qint16> samples;
    double          sampleRate = 0.0;
    int             channel    = 0;
};

Q_DECLARE_METATYPE(WaveformData)
