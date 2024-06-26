#include "CFUP.h"
#include "CFUPManager.h"
#include <QDateTime>
#include <QThread>
#include "tools/tools.h"

#define THREAD_CHECK(ret) if (!threadCheck_(__FUNCTION__))return ret

CFUP::CFUP(CFUPManager *parent, const QHostAddress &IP, unsigned short p) : QObject(parent), IP(IP), port(p), cm(parent) {
    connect(&hbt, &QTimer::timeout, this, [&]() {
        if (cs == 1) {
            auto *cdpt = newCDPT_();
            cdpt->cf = 0x05;
            cdpt->SID = ID + sendWnd.size() + sendBufLv1.size();
            sendBufLv1.append(cdpt);
            updateWnd_();
        }
    });
}

bool CFUP::threadCheck_(const QString &funcName) {
    if (QThread::currentThread() == thread())return true;
    qWarning()
            << "函数" << funcName << "不允许在其他线程调用, 操作被拒绝.\n"
            << "对象:" << this << ", 调用线程:" << QThread::currentThread() << ", 对象所在线程:" << thread();
    return false;
}

QHostAddress CFUP::getIP() {
    THREAD_CHECK(QHostAddress::Null);
    return IP;
}

unsigned short CFUP::getPort() {
    THREAD_CHECK(0);
    return port;
}

void CFUP::proc_(const QByteArray &data) { // 该函数只能被CFUPManager调用
    unsigned char cf = data[0];

    bool UDL = ((cf >> 7) & 0x01);
    bool UD = ((cf >> 6) & 0x01);
    bool NA = ((cf >> 5) & 0x01);
    bool RT = ((cf >> 4) & 0x01);
    auto cmd = (unsigned char) (cf & (unsigned char) 0x07);

    if (NA && RT)return;
    if (1 <= cmd && cmd <= 5 && !UDL) {
        if (cmd == 1)cmdRC_(data); // RC指令, 请求
        if (cmd == 2)cmdACK_(NA, data); // ACK指令, 应答
        if (cmd == 3)cmdRC_ACK_(RT, data); // RC ACK指令, 请求应答
        if (cmd == 4)cmdC_(NA, UD, data); // C指令, 断开
        if (cmd == 5)cmdH_(RT, data); // H命令, 心跳包
    } else {
        if (!NA && UD) {//需要回复, 有用户数据
            if (data.size() <= 11)return;
            unsigned short SID = (*(unsigned short *) (data.data() + 1));
            long long time = *(long long *) (data.data() + 3);
            if (!time_(SID, time))return;
            NA_ACK_(SID);
            if (recvWnd.contains(SID) && !RT)close("窗口数据发生重叠"); // 如果窗口包含该数据而且不是重发包
            else if (!RT || !recvWnd.contains(SID)) { //如果是重发包，并且接收窗口中已经有该数据，则不需要再次存储
                // 从数据包中提取用户数据，跳过前三个字节的头部信息
                recvWnd[SID] = {cf, SID, data.mid(11)};
            }
        } else if (UD) {//有用户数据
            if (data.size() <= 1)return;
            readBuf.append(data.mid(1));
            emit readyRead();
        }
    }
    updateWnd_();
}

void CFUP::send(const QByteArray &data) {
    THREAD_CHECK();
    if (cs != 1 || data.isEmpty())return;
    sendBufLv2.append(data);
    QTimer::singleShot(0, [this]() { updateWnd_(); });
}

void CFUP::sendNow(const QByteArray &data) {
    THREAD_CHECK();
    if (cs != 1 || data.isEmpty())return;
    auto *tmp = new CDPT(this);
    tmp->data = data;
    tmp->cf = 0x60;
    sendPackage_(tmp);
    delete tmp;
}

void CFUP::connectToHost_() { // 该函数只能被CFUPManager调用
    if (cs != -1)return;
    initiative = true;
    auto cdpt = newCDPT_();
    cdpt->SID = 0;
    cdpt->cf = (char) 0x01;
    sendBufLv1.append(cdpt); // 直接放入一级缓存
    cs = 0; // 半连接
    updateWnd_();
}

void CFUP::close(const QByteArray &data) {
    THREAD_CHECK();
    if (cs != 2) {
        auto cdpt = new CDPT(this);
        cdpt->cf = 0x24;
        if (!data.isEmpty()) {
            cdpt->cf |= 0x40;
            cdpt->data = data;
        }
        sendPackage_(cdpt);
        delete cdpt;
        cs = 2;
    }
    for (auto i: sendWnd)i->deleteLater();
    for (auto i: sendBufLv1)i->deleteLater();
    sendWnd.clear();
    sendBufLv1.clear();
    sendBufLv2.clear();
    hbt.stop();
    emit disconnected(data);
}

void CFUP::updateWnd_() {
    // 更新发送窗口
    while (sendWnd.contains(ID)) { // 释放掉已经接收停止的数据包
        if (sendWnd[ID]->isActive())break; // 如果数据包还未被接收, break
        delete sendWnd[ID]; // 释放内存
        sendWnd.remove(ID); // 移除
        ID++; // ID++
    }
    updateSendBuf_(); // 更新发送缓存
    while ((sendWnd.size() < wndSize) && (!sendBufLv1.isEmpty())) { // 循环添加一级缓存的数据包
        auto cdpt = sendBufLv1.front(); // 取首元素
        sendBufLv1.pop_front();
        sendWnd[cdpt->SID] = cdpt; // 放到发送窗口
        sendPackage_(cdpt); // 发送数据包
        cdpt->start(timeout); // 启动定时器
    }
    while (recvWnd.contains(OID + 1)) { // 如果接收到了数据
        OID++; // OID++
        recvBuf.append(recvWnd[OID].data); // 先添加进来数据
        if (!((recvWnd[OID].cf >> 7) & 0x01)) { // 如果不是链表包
            readBuf.append(recvBuf); // 添加到可读缓存
            recvBuf.clear(); // 清空接收缓存
        }
        recvWnd.remove(OID); // 移除当前数据包
    }
    if (!readBuf.isEmpty())emit readyRead();
}

void CFUP::sendPackage_(CDPT *cdpt) { // 只负责构造数据包和发送
    QByteArray data;
    data.append((char) cdpt->cf);
    unsigned char cmd = (char) (cdpt->cf & (char) 0x07);
    bool NA = (cdpt->cf >> 5) & 0x01;
    if (!NA) {
        data += dump(cdpt->SID);
        data += dump(QDateTime::currentMSecsSinceEpoch()); // 发送时间
    }
    if ((cmd == 2) || (cmd == 3))data += dump(cdpt->AID);
    if ((cdpt->cf >> 6) & 0x01)data += cdpt->data;
    cm->send_(IP, port, data);
}

void CFUP::updateSendBuf_() { // 更新发送缓存
    // 从二级缓存解包到一级缓存
    if (!sendBufLv1.isEmpty() || sendBufLv2.isEmpty())return;
    auto data = sendBufLv2.front(); // 拿一个数据
    sendBufLv2.pop_front();
    // 全部序列化到一级缓存
    if (data.size() <= dataBlockSize) { // 数据包长度小于块大小
        auto cdpt = newCDPT_();
        cdpt->data = data;
        cdpt->cf = 0x40;
        cdpt->SID = ID + sendWnd.size();
        sendBufLv1.append(cdpt);
    } else { // 否则进行拆包
        QByteArrayList dataBlock; // 数据块
        QByteArray i = data, tmp;
        while (!i.isEmpty()) { // 迭代
            unsigned short dbs = dataBlockSize;
            if (i.size() <= dataBlockSize)dbs = i.size(); // 计算实际数据块大小
            tmp.append(i, dbs); // 赋值tmp
            dataBlock.append(tmp); // 添加到数据块
            tmp.clear(); // 清空tmp
            i = i.mid(dbs); // 迭代
        }
        auto baseID = sendWnd.size(); // 获取当前窗口长度
        for (qsizetype j = 0; j < dataBlock.size(); j++) {
            auto cdpt = newCDPT_();
            cdpt->data = dataBlock[j];
            cdpt->SID = ID + j + baseID;
            if (j != dataBlock.size() - 1)cdpt->cf = 0xC0; // 链表包
            else cdpt->cf = 0x40; // 非链表包
            sendBufLv1.append(cdpt);
        }
    }
}

CDPT *CFUP::newCDPT_() {
    auto *cdpt = new CDPT(this);
    connect(cdpt, &CDPT::timeout, this, &CFUP::sendTimeout_);
    return cdpt;
}

void CFUP::sendTimeout_() { // 只做重发包逻辑和重试次数过多逻辑
    auto cdpt = (CDPT *) sender();
    if (cdpt->retryNum < retryNum) {
        cdpt->retryNum++;
        cdpt->cf |= 0x10;
        sendPackage_(cdpt);
    } else close("对方应答超时");
}

QByteArray CFUP::nextPendingData() {
    THREAD_CHECK({});
    auto tmp = readBuf.front();
    readBuf.pop_front();
    return tmp;
}

bool CFUP::hasData() {
    THREAD_CHECK(false);
    return !readBuf.isEmpty();
}

QByteArrayList CFUP::readAll() {
    THREAD_CHECK({});
    auto tmp = readBuf;
    readBuf.clear();
    return tmp;
}

void CFUP::NA_ACK_(unsigned short AID) {
    auto cdpt = new CDPT(this);
    cdpt->AID = AID;
    cdpt->cf = (char) 0x22;
    sendPackage_(cdpt);
    delete cdpt;
}

bool CFUP::time_(unsigned short SID, long long time) {
    if (recvLastTime.contains(SID)) {
        if (recvLastTime[0] >= time)return false;
        return true;
    }
    recvLastTime[SID] = time;
    return true;
}

CFUP::~CFUP() = default;

CDPT::CDPT(QObject *parent) : QTimer(parent) {}

CDPT::~CDPT() = default;
