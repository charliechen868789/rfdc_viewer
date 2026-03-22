#pragma once
#include <QVector>
#include <cstdint>

class WaveformGenerator
{
public:
    /**
     * Generate a normalised sine tone.
     * Returns the sample vector and sets trimmedSamples to the last
     * zero-crossing index (replicating the Python behaviour).
     */
    static QVector<qint16> generateToneNormalized(
        qint64  freqHz,
        double  thetaRad,
        double  sampleRateHz,
        int     sampleNum,
        int    &trimmedSamples);
};
