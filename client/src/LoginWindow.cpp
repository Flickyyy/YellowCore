#include "LoginWindow.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QFont>

LoginWindow::LoginWindow(TcpClient* client, QWidget* parent)
    : QDialog(parent), m_client(client)
{
    setWindowTitle("YellowCore — Login");
    setFixedSize(380, 310);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(20, 20, 20, 20);

    auto* title = new QLabel("YellowCore Trading Platform");
    title->setAlignment(Qt::AlignCenter);
    QFont f = title->font();
    f.setPointSize(13);
    f.setBold(true);
    title->setFont(f);
    root->addWidget(title);

    // Server connection
    auto* connBox = new QGroupBox("Server");
    auto* connRow = new QHBoxLayout(connBox);
    m_hostEdit = new QLineEdit("localhost");
    m_portEdit = new QLineEdit("9090");
    m_portEdit->setFixedWidth(56);
    connRow->addWidget(new QLabel("Host:"));
    connRow->addWidget(m_hostEdit);
    connRow->addSpacing(8);
    connRow->addWidget(new QLabel("Port:"));
    connRow->addWidget(m_portEdit);
    root->addWidget(connBox);

    // Credentials
    auto* credBox = new QGroupBox("Credentials");
    auto* credForm = new QFormLayout(credBox);
    credForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_usernameEdit = new QLineEdit;
    m_usernameEdit->setPlaceholderText("username");
    m_passwordEdit = new QLineEdit;
    m_passwordEdit->setPlaceholderText("password");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    credForm->addRow("Username:", m_usernameEdit);
    credForm->addRow("Password:", m_passwordEdit);
    root->addWidget(credBox);

    // Buttons
    auto* btnRow = new QHBoxLayout;
    auto* loginBtn    = new QPushButton("Login");
    auto* registerBtn = new QPushButton("Register");
    loginBtn->setDefault(true);
    loginBtn->setFixedHeight(32);
    registerBtn->setFixedHeight(32);
    btnRow->addWidget(loginBtn);
    btnRow->addWidget(registerBtn);
    root->addLayout(btnRow);

    // Status line
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_statusLabel);

    connect(loginBtn,    &QPushButton::clicked, this, &LoginWindow::onLogin);
    connect(registerBtn, &QPushButton::clicked, this, &LoginWindow::onRegister);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginWindow::onLogin);
}

bool LoginWindow::ensureConnected() {
    if (m_client->isConnected()) return true;
    setStatus("Connecting…");
    if (!m_client->connectToServer(m_hostEdit->text(), m_portEdit->text().toInt())) {
        setStatus("Cannot connect to server", true);
        return false;
    }
    return true;
}

void LoginWindow::setStatus(const QString& msg, bool error) {
    m_statusLabel->setText(msg);
    m_statusLabel->setStyleSheet(error ? "color:#c0392b;font-weight:bold;"
                                       : "color:#27ae60;");
}

void LoginWindow::onLogin() {
    if (m_usernameEdit->text().isEmpty() || m_passwordEdit->text().isEmpty()) {
        setStatus("Username and password required", true);
        return;
    }
    if (!ensureConnected()) return;

    setStatus("Authenticating…");
    auto resp = m_client->sendRequest({
        {"type",     "login"},
        {"username", m_usernameEdit->text()},
        {"password", m_passwordEdit->text()}
    });

    if (resp["status"].toString() == "ok") {
        m_token         = resp["token"].toString();
        m_usernameValue = m_usernameEdit->text();
        setStatus("OK");
        accept();
    } else {
        setStatus(resp["message"].toString(), true);
    }
}

void LoginWindow::onRegister() {
    if (m_usernameEdit->text().isEmpty() || m_passwordEdit->text().isEmpty()) {
        setStatus("Username and password required", true);
        return;
    }
    if (!ensureConnected()) return;

    setStatus("Registering…");
    auto resp = m_client->sendRequest({
        {"type",     "register"},
        {"username", m_usernameEdit->text()},
        {"password", m_passwordEdit->text()}
    });

    if (resp["status"].toString() == "ok") {
        setStatus("Registered — logging in…");
        onLogin();
    } else {
        setStatus(resp["message"].toString(), true);
    }
}
