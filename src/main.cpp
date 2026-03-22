#include <QApplication>
#include <QMetaType>
#include "mainwindow.h"
#include "commontypes.h"

int main(int argc, char *argv[])
{
    // Register WaveformData so it can cross thread boundaries via queued signals
    qRegisterMetaType<WaveformData>("WaveformData");

    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setApplicationName("Avnet RFSoC Evaluation Tool");
    app.setOrganizationName("Avnet");

    MainWindow win;
    win.show();
    return app.exec();
}
