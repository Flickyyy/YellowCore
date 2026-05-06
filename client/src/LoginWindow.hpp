#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include "TcpClient.hpp"

class LoginWindow : public QDialog {
    Q_OBJECT
public:
    explicit LoginWindow(TcpClient* client, QWidget* parent = nullptr);

    QString token()    const { return m_token; }
    QString username() const { return m_usernameValue; }

private slots:
    void onLogin();
    void onRegister();

private:
    TcpClient* m_client;

    QLineEdit* m_hostEdit;
    QLineEdit* m_portEdit;
    QLineEdit* m_usernameEdit;
    QLineEdit* m_passwordEdit;
    QLabel*    m_statusLabel;

    QString m_token;
    QString m_usernameValue;

    bool ensureConnected();
    void setStatus(const QString& msg, bool error = false);
};
