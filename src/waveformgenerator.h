#pragma once
#include <QVector>
#include <cstdint>

class WaveformGenerator
{
public:
    enum class Shape { Sine, Square };

    /**
     * Generate a normalised waveform (sine or square).
     * trimmedSamples is set to the last zero-crossing index.
     * dutyCycle only applies to Square (0.0–1.0, default 0.5).
     */
    static QVector<qint16> generateNormalized(
        Shape   shape,
        qint64  freqHz,
        double  thetaRad,
        double  sampleRateHz,
        int     sampleNum,
        int    &trimmedSamples,
        double  dutyCycle = 0.5);

    // Convenience wrappers (keep old name working)
    static QVector<qint16> generateToneNormalized(
        qint64  freqHz,
        double  thetaRad,
        double  sampleRateHz,
        int     sampleNum,
        int    &trimmedSamples);

    static QVector<qint16> generateSquareNormalized(
        qint64  freqHz,
        double  thetaRad,
        double  sampleRateHz,
        int     sampleNum,
        int    &trimmedSamples,
        double  dutyCycle = 0.5);
};
