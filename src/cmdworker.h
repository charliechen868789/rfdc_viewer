#pragma once
#include <QThread>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QByteArray>

// A command with an optional post-send delay
struct CmdItem {
    QByteArray cmd;
    int        delayAfterMs = 0;  // ms to wait after sending this command
    bool       finalItem    = false; // if true, emit sequenceDone after delay
};

class CmdWorker : public QThread
{
    Q_OBJECT
public:
    explicit CmdWorker(const QString &host, quint16 port, QObject *parent = nullptr);
    ~CmdWorker() override;

    // Single command, no delay
    void enqueueCommand(const QByteArray &cmd);

    // Sequence: each item sent with its delay; sequenceDone() emitted at end
    void enqueueSequence(const QList<CmdItem> &items);

    void stop();

    static constexpr int CONNECT_TIMEOUT_MS  = 3000;
    static constexpr int WRITE_TIMEOUT_MS    = 1000;
    static constexpr int READ_POLL_MS        = 100;
    static constexpr int RECONNECT_DELAY_MS  = 2000;
    static constexpr int IDLE_WAIT_MS        = 50;

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &msg);
    void sequenceDone();   // emitted after the last item in a sequence

protected:
    void run() override;

private:
    QString  m_host;
    quint16  m_port;
    bool     m_stop = false;

    QMutex           m_mutex;
    QWaitCondition   m_cond;
    QQueue<CmdItem>  m_queue;
};
