#pragma once

#include <QHostAddress>
#include <QObject>
#include <QHash>

class CFUP;
class QUdpSocket;

class CFUPManager final : public QObject {
Q_OBJECT

public:
    explicit CFUPManager(QObject * = nullptr);

    QString bind(const QString &, unsigned short); // 绑定

    QStringList bind(unsigned short); // 绑定

    void setMaxConnectNum(int); // 设置最大连接数量

    int getMaxConnectNum(); // 获取最大连接数量

    int getConnectedNum(); // 获取已连接数量

    int isBind(); // 已经绑定, 0表示无绑定, 1表示只绑定了IPv4, 2表示只绑定了IPv6, 3表示IPv4和IPv6都绑定了

    void connectToHost(const QString &, unsigned short);

    void connectToHost(const QHostAddress &, unsigned short);

signals:

    void connectFail(const QHostAddress &, unsigned short, const QByteArray &); // 我方主动连接连接失败

    void connected(CFUP *); // 连接成功(包含我方主动与对方请求)

    void cLog(const QString &);

public slots:

    void close(); // 这个只是关闭管理器

    void quit(); // delete调用它
private slots:

    void deleteLater();

    void recv_(); // 接收数据

    void rmCFUP_();
private:
    QHash<QString, CFUP *> cfup; // 已连接的
    int connectNum = 65535; // 最大连接数量
    QHash<QString, CFUP *> connecting; // 连接中的cfup
    QUdpSocket *ipv4 = nullptr;
    QUdpSocket *ipv6 = nullptr;
    bool isBindAll = false; // 判断是否是调用的QStringList bind(unsigned short);函数

    ~CFUPManager() override;

    void proc_(const QHostAddress &, unsigned short, const QByteArray &); // 处理来的信息

    void send_(const QHostAddress &, unsigned short, const QByteArray &); // 发送数据

    bool threadCheck_(const QString &); // 线程检查

    void cfupConnected_(CFUP *);

    void requestInvalid_(const QByteArray &);

    friend class CFUP;
};
