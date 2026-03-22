#pragma once
#include <QString>

namespace Constant {

constexpr char   BOARD_IP[]      = "192.168.1.113";
constexpr quint16 DATA_PORT      = 8083;
constexpr quint16 CMD_PORT       = 8084;

constexpr double DAC_FS          = 9824e6;   // Hz
constexpr int    DAC_SAMPLE_NUM  = 8192;

constexpr double ADC_FS          = 2456e6;   // Hz
constexpr int    ADC_SAMPLE_NUM  = 8192;

constexpr int    NUM_CHANNELS    = 16;

} // namespace Constant
