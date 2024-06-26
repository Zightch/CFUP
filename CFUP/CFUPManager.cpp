#include "CFUPManager.h"
#include "tools/tools.h"
#include "CFUP.h"
#include <QDateTime>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QThread>

#define THREAD_CHECK(ret) if (!threadCheck_(__FUNCTION__))return ret

void CFUPManager::proc_(const QHostAddress &IP, unsigned short port, const QByteArray &data) { // 来源于recv_调用, 不会被别的线程调用, 是私有函数
    auto ipPort = IPPort(IP, port); // 转字符串
    if (ipPort.isEmpty())return; // 转换失败
    if (cfup.contains(ipPort) || connecting.contains(ipPort)) { // 如果已经存在对象
        if (cfup.contains(ipPort))cfup[ipPort]->proc_(data);
        if (connecting.contains(ipPort))connecting[ipPort]->proc_(data);
        return;
    }
    if (data.size() != 11)return; // 长度不正确
    char cf = data[0];
    if (cf != 0x11 && cf != 0x01)return; // 如果不是连接请求, 直接丢弃
    unsigned short SID = (*(unsigned short *) (data.data() + 1)); // 提取SID
    if (SID != 0)return; // SID必须是0
    if (cfup.size() >= connectNum)return; // 连接上限
    auto tmp = new CFUP(this, IP, port);
    connecting[ipPort] = tmp;
    connect(tmp, &CFUP::disconnected, this, &CFUPManager::requestInvalid_);
    tmp->proc_(data);
}

CFUPManager::CFUPManager(QObject *parent) : QObject(parent) {}

CFUPManager::~CFUPManager() = default; // 不允许被外部调用

void CFUPManager::deleteLater() {QObject::deleteLater();} // 不允许被外部调用

void CFUPManager::close() { // 这个只是关闭管理器
    THREAD_CHECK();
    auto rm = [this](QHash<QString, CFUP *> &cs) {
        for (auto i: cs) {
            disconnect(i, &CFUP::disconnected, this, &CFUPManager::requestInvalid_);
            disconnect(i, &CFUP::disconnected, this, &CFUPManager::rmCFUP_);
            i->close("管理器服务关闭");
            if (i->cs < 1)i->deleteLater(); // 如果i还处于未连接状态, 自己delete
        }
        cs.clear();
    };
    rm(cfup);
    rm(connecting);
    if (ipv4 != nullptr)ipv4->deleteLater();
    if (ipv6 != nullptr)ipv6->deleteLater();
    ipv4 = nullptr;
    ipv6 = nullptr;
}

void CFUPManager::quit() { // delete对象调用它
    THREAD_CHECK();
    close();
    deleteLater();
}

QString CFUPManager::bind(const QString &ipStr, unsigned short port) {
    THREAD_CHECK({}); // 不允许被别的线程调用
    if (isBind() != 0 && !isBindAll)return "CFUP管理器已绑定";
    QHostAddress ip(ipStr); // 构造QHostAddress对象
    QUdpSocket **udpTmp = nullptr; // 使用哪个udp, 双重指针
    auto protocol = ip.protocol(); // 获取ip的协议
    if (protocol == QUdpSocket::IPv4Protocol)udpTmp = &ipv4; // 如果是ipv4, 获取ipv4的udp指针
    else if (protocol == QUdpSocket::IPv6Protocol)udpTmp = &ipv6; // 如果是ipv6, 获取ipv6的udp指针
    if (udpTmp == nullptr) return "IP不正确"; // 如果udpTmp为空, 说明IP不正确
    auto &udp = (*udpTmp); // 获取udpTmp指向的指针对象
    QString error; // 错误信息
    if (udp == nullptr) { // 如果udp是空
        udp = new QUdpSocket(this); // new对象
        if (udp->bind(ip, port)) // 绑定
            connect(udp, &QUdpSocket::readyRead, this, &CFUPManager::recv_);
        else { // 绑定失败
            error = udp->errorString();
            delete udp;
            udp = nullptr;
        }
    } else error = "CFUP管理器已绑定"; // 否则CFUP已绑定
    return error;
}

QStringList CFUPManager::bind(unsigned short port) { // 同时绑定ipv4和ipv6
    THREAD_CHECK({}); // 不允许被别的线程调用
    isBindAll = true;
    QStringList tmp;
    auto tmp4 = bind("0.0.0.0", port);
    if (!tmp4.isEmpty())tmp.append("IPv4: " + tmp4);
    auto tmp6 = bind("::", port);
    if (!tmp6.isEmpty())tmp.append("IPv6: " + tmp6);
    isBindAll = false;
    return tmp;
}

void CFUPManager::connectToHost(const QString &ipStr, unsigned short port) {
    THREAD_CHECK();
    connectToHost(QHostAddress(ipStr), port);
}

void CFUPManager::connectToHost(const QHostAddress &ip, unsigned short port) {
    THREAD_CHECK(); // 检查线程
    QUdpSocket *udp = nullptr;
    auto protocol = ip.protocol();
    if (protocol == QUdpSocket::IPv4Protocol)udp = ipv4;
    else if (protocol == QUdpSocket::IPv6Protocol)udp = ipv6;
    if (udp == nullptr) { // IP协议检查失败
        emit connectFail(ip, port, "以目标IP协议所管理的CFUP管理器未绑定");
        return;
    }
    if ((cfup.size() >= connectNum)) {
        emit connectFail(ip, port, "当前管理器连接的CFUP数量已达到上限");
        return;
    }
    auto ipPort = IPPort(ip, port);
    if (cfup.contains(ipPort)) {
        emit connected(cfup[ipPort]);
        return;
    }
    if (!connecting.contains(ipPort)) {
        auto tmp = new CFUP(this, ip, port);
        connecting[ipPort] = tmp;
        connect(tmp, &CFUP::disconnected, this, &CFUPManager::requestInvalid_);
        tmp->connectToHost_();
    }
}

void CFUPManager::recv_() { // 来源于udpSocket信号调用, 不会被别的线程调用, 是私有函数
    auto udp = (QUdpSocket *) sender();
    while (udp->hasPendingDatagrams()) {
        auto datagrams = udp->receiveDatagram();
        auto IP = datagrams.senderAddress();
        auto port = datagrams.senderPort();
        auto data = datagrams.data();
        if (!data.isEmpty()) {
            emit cLog("↓ " + IPPort(IP, port) + " : " + bytesToHexString(data));
            proc_(IP, port, data);
        }
    }
}

void CFUPManager::setMaxConnectNum(int num) {
    THREAD_CHECK(); // 不允许被别的线程调用
    if (num > 0)connectNum = num;
}

int CFUPManager::getMaxConnectNum() {
    THREAD_CHECK(-1); // 不允许被别的线程调用
    return connectNum;
}

int CFUPManager::getConnectedNum() {
    THREAD_CHECK(-1); // 不允许被别的线程调用
    return (int)cfup.size();
}

int CFUPManager::isBind() { // 已经绑定, 1表示只绑定了IPv4, 2表示只绑定了IPv6, 3表示IPv4和IPv6都绑定了
    THREAD_CHECK(-1); // 不允许被别的线程调用
    int tmp = 0;
    if (ipv4 != nullptr)tmp |= 1;
    if (ipv6 != nullptr)tmp |= 2;
    return tmp;
}

void CFUPManager::send_(const QHostAddress &IP, unsigned short port, const QByteArray &data) {
    QUdpSocket *udp = nullptr;
    auto protocol = IP.protocol();
    if (protocol == QUdpSocket::IPv4Protocol)udp = ipv4;
    else if (protocol == QUdpSocket::IPv6Protocol)udp = ipv6;
    if (udp == nullptr)return;
    udp->writeDatagram(data, IP, port);
    emit cLog("↑ " + IPPort(IP, port) + " : " + bytesToHexString(data));
}

bool CFUPManager::threadCheck_(const QString &funcName) {
    if (QThread::currentThread() == thread())return true;
    qWarning()
            << "函数" << funcName << "不允许在其他线程调用, 操作被拒绝.\n"
            << "对象:" << this << ", 调用线程:" << QThread::currentThread() << ", 对象所在线程:" << thread();
    return false;
}

void CFUPManager::cfupConnected_(CFUP *c) { // 当CFUP处理后连接成功调用这个函数
    auto key = IPPort(c->IP, c->port);
    connecting.remove(key);
    if (cfup.size() < connectNum) {
        disconnect(c, &CFUP::disconnected, this, &CFUPManager::requestInvalid_); // 断开连接
        connect(c, &CFUP::disconnected, this, &CFUPManager::rmCFUP_);
        cfup[key] = c;
        emit connected(c);
    } else {
        c->close("当前连接的CFUP数量已达到上限");
        c->deleteLater();
        if (c->initiative)emit connectFail(c->IP, c->port, "当前连接的CFUP数量已达到上限");
    }
}

void CFUPManager::requestInvalid_(const QByteArray &data) {
    auto c = (CFUP *) sender();
    c->deleteLater();
    connecting.remove(IPPort(c->IP, c->port));
    if (c->initiative)emit connectFail(c->IP, c->port, data); // 如果是主动连接的触发连接失败
}

void CFUPManager::rmCFUP_() {
    auto c = (CFUP *) sender();
    cfup.remove(IPPort(c->IP, c->port));
}
