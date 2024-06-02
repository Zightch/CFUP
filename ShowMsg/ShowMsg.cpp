#include "ShowMsg.h"
#include "ui_ShowMsg.h"
#include "tools/tools.h"
#include <QRegularExpression>

ShowMsg::ShowMsg(CCP *ccp, QWidget *parent) : QWidget(parent), ui(new Ui::ShowMsg) {
    ui->setupUi(this);
    this->ccp = ccp;
    setWindowTitle(IPPort(ccp->getIP(), ccp->getPort()));
    connect(ccp, &CCP::readyRead, this, &ShowMsg::recv);
    connect(ui->send, &QPushButton::clicked, this, &ShowMsg::send);
    connect(ui->recvIsHex, &QCheckBox::checkStateChanged, this, &ShowMsg::hex);
    connect(ui->sendIsHex, &QCheckBox::checkStateChanged, this, &ShowMsg::hex);
    connect(ui->sendData,&QPlainTextEdit::textChanged,this,&ShowMsg::sendDataChange);
}

ShowMsg::~ShowMsg() {
    delete ui;
}

CCP *ShowMsg::getCCP() {
    return ccp;
}

void ShowMsg::recv() {
    while (ccp->hasData()) {
        auto data = ccp->nextPendingData();
        recvData.append(data);
        if (ui->recvIsHex->isChecked())
            ui->recvData->appendPlainText(bytesToHexString(data));
        else
            ui->recvData->appendPlainText(QString::fromLocal8Bit(data));
    }
}

void ShowMsg::send() {
    auto data = ui->sendData->toPlainText();
    if (ui->sendIsHex->isChecked())
        ccp->send(hexStringToBytes(data));
    else
        ccp->send(data.toUtf8());
}

void ShowMsg::hex(Qt::CheckState state) {
    auto checkBox = (QCheckBox *) sender();
    if (checkBox == ui->recvIsHex) {
        if (state == Qt::Unchecked) { // 未选中
            ui->recvData->clear();
            for (const auto &i: recvData)
                ui->recvData->appendPlainText(QString::fromLocal8Bit(i));
        }
        if (state == Qt::Checked) { // 选中
            ui->recvData->clear();
            for (const auto &i: recvData)
                ui->recvData->appendPlainText(bytesToHexString(i));
        }
    }
    if (checkBox == ui->sendIsHex) {
        if (state == Qt::Unchecked) // 未选中
            ui->sendData->setPlainText(hexStringToBytes(ui->sendData->toPlainText())); // 16进制转普通字符
        if (state == Qt::Checked) // 选中
            ui->sendData->setPlainText(bytesToHexString(ui->sendData->toPlainText().toUtf8())); // 普通字符转16进制
    }
}

void ShowMsg::sendDataChange() {
    const static QRegularExpression regExp(R"(^(([0-9A-Fa-f]{2}\s+)+)?[0-9A-Fa-f]{0,2}$)");
    auto data = ui->sendData->toPlainText();
    if (data.isEmpty())return;
    if (!ui->sendIsHex->isChecked())return;
    auto match = regExp.match(data);
    if (match.hasMatch())sendLastHexStr = data;
    else ui->sendData->setPlainText(sendLastHexStr);
    QTextCursor cursor = ui->sendData->textCursor();
    cursor.movePosition(QTextCursor::End);
    ui->sendData->setTextCursor(cursor);
}
