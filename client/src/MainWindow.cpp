#include "MainWindow.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QHeaderView>
#include <QStatusBar>
#include <QMessageBox>
#include <QDateTime>
#include <QFont>
#include <QtMath>

// ─── constructor ─────────────────────────────────────────────────────────────

MainWindow::MainWindow(TcpClient* client,
                       const QString& token,
                       const QString& username,
                       QWidget* parent)
    : QMainWindow(parent), m_client(client), m_token(token), m_username(username)
{
    setWindowTitle(QString("YellowCore — %1").arg(username));
    resize(960, 680);

    auto* central = new QWidget;
    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(8, 8, 8, 0);
    vbox->setSpacing(6);

    // Header row
    auto* hdr = new QHBoxLayout;
    auto* userLbl = new QLabel(QString("Logged in as  <b>%1</b>").arg(username));
    auto* logoutBtn = new QPushButton("Logout");
    logoutBtn->setFixedWidth(80);
    hdr->addWidget(userLbl);
    hdr->addStretch();
    hdr->addWidget(logoutBtn);
    vbox->addLayout(hdr);

    auto* tabs = new QTabWidget;
    tabs->addTab(buildAccountsTab(), "Accounts");
    tabs->addTab(buildMarketTab(),   "Market");
    tabs->addTab(buildPortfolioTab(), "Portfolio");
    tabs->addTab(buildHistoryTab(),  "History");
    vbox->addWidget(tabs);

    setCentralWidget(central);
    statusBar()->showMessage("Connected");

    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer,   &QTimer::timeout,            this, &MainWindow::onTimerTick);
    connect(logoutBtn, &QPushButton::clicked,        this, &MainWindow::onLogout);
    connect(tabs,      &QTabWidget::currentChanged,  this, &MainWindow::onTabChanged);

    refreshAccounts();
    // Timer is only running while the Market tab is active; see onTabChanged.
}

// ─── tab builders ────────────────────────────────────────────────────────────

static QTableWidget* makeTable(const QStringList& headers) {
    auto* t = new QTableWidget(0, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->setVisible(false);
    return t;
}

QWidget* MainWindow::buildAccountsTab() {
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setSpacing(8);

    m_accountsTable = makeTable({"Account ID", "Currency", "Balance"});

    auto* topRow = new QHBoxLayout;
    auto* refreshBtn = new QPushButton("Refresh");
    refreshBtn->setFixedWidth(80);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshAccounts);
    topRow->addWidget(m_accountsTable);

    // Create account
    auto* createBox = new QGroupBox("New Account");
    auto* createRow = new QHBoxLayout(createBox);
    m_newCurrencyCombo = new QComboBox;
    m_newCurrencyCombo->addItems({"RUB", "USD", "EUR"});
    auto* createBtn = new QPushButton("Create");
    createBtn->setFixedWidth(80);
    connect(createBtn, &QPushButton::clicked, this, &MainWindow::onCreateAccount);
    createRow->addWidget(new QLabel("Currency:"));
    createRow->addWidget(m_newCurrencyCombo);
    createRow->addStretch();
    createRow->addWidget(createBtn);

    // Operations
    auto* opsBox = new QGroupBox("Account Operations");
    auto* opsForm = new QFormLayout(opsBox);
    opsForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_selAccount = new QComboBox;

    m_depositAmt = new QDoubleSpinBox;
    m_depositAmt->setRange(0.01, 1e8);
    m_depositAmt->setDecimals(2);
    m_depositAmt->setValue(1000.0);

    m_withdrawAmt = new QDoubleSpinBox;
    m_withdrawAmt->setRange(0.01, 1e8);
    m_withdrawAmt->setDecimals(2);
    m_withdrawAmt->setValue(100.0);

    m_transferToCombo = new QComboBox;
    m_transferAmt = new QDoubleSpinBox;
    m_transferAmt->setRange(0.01, 1e8);
    m_transferAmt->setDecimals(2);
    m_transferAmt->setValue(100.0);

    auto* depBtn  = new QPushButton("Deposit");
    auto* wdBtn   = new QPushButton("Withdraw");
    auto* xfrBtn  = new QPushButton("Transfer");
    depBtn->setFixedWidth(80);
    wdBtn->setFixedWidth(80);
    xfrBtn->setFixedWidth(80);
    connect(depBtn,  &QPushButton::clicked, this, &MainWindow::onDeposit);
    connect(wdBtn,   &QPushButton::clicked, this, &MainWindow::onWithdraw);
    connect(xfrBtn,  &QPushButton::clicked, this, &MainWindow::onTransfer);

    auto makeRow = [](QWidget* widget, QPushButton* btn) {
        auto* row = new QHBoxLayout;
        row->addWidget(widget);
        row->addWidget(btn);
        auto* w2 = new QWidget;
        w2->setLayout(row);
        return w2;
    };

    auto* xfrRow = new QHBoxLayout;
    xfrRow->addWidget(new QLabel("To:"));
    xfrRow->addWidget(m_transferToCombo, 1);
    xfrRow->addWidget(m_transferAmt);
    xfrRow->addWidget(xfrBtn);
    auto* xfrWidget = new QWidget;
    xfrWidget->setLayout(xfrRow);

    opsForm->addRow("Account:", m_selAccount);
    opsForm->addRow("Deposit:",  makeRow(m_depositAmt,  depBtn));
    opsForm->addRow("Withdraw:", makeRow(m_withdrawAmt, wdBtn));
    opsForm->addRow("Transfer:", xfrWidget);

    root->addWidget(m_accountsTable, 2);
    root->addWidget(refreshBtn, 0, Qt::AlignRight);
    root->addWidget(createBox);
    root->addWidget(opsBox);

    return w;
}

QWidget* MainWindow::buildMarketTab() {
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setSpacing(8);

    m_quotesTable = makeTable({"Ticker", "Price (USD)"});
    m_quotesTable->setFixedHeight(220);

    auto* tradeBox = new QGroupBox("Place Order");
    auto* form = new QFormLayout(tradeBox);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_tradeAccountCombo = new QComboBox;
    m_tickerCombo = new QComboBox;
    m_tickerCombo->addItems({"AAPL","GOOGL","TSLA","AMZN","MSFT","NFLX","META","NVDA"});
    m_qtySpinBox = new QSpinBox;
    m_qtySpinBox->setRange(1, 100000);
    m_qtySpinBox->setValue(1);

    auto* buyBtn  = new QPushButton("Buy");
    auto* sellBtn = new QPushButton("Sell");
    buyBtn->setFixedWidth(90);
    sellBtn->setFixedWidth(90);
    buyBtn->setStyleSheet(
        "QPushButton{background:#27ae60;color:white;font-weight:bold;border-radius:4px;}"
        "QPushButton:hover{background:#2ecc71;}");
    sellBtn->setStyleSheet(
        "QPushButton{background:#c0392b;color:white;font-weight:bold;border-radius:4px;}"
        "QPushButton:hover{background:#e74c3c;}");
    connect(buyBtn,  &QPushButton::clicked, this, &MainWindow::onBuyStock);
    connect(sellBtn, &QPushButton::clicked, this, &MainWindow::onSellStock);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(buyBtn);
    btnRow->addWidget(sellBtn);
    btnRow->addStretch();
    auto* btnWidget = new QWidget;
    btnWidget->setLayout(btnRow);

    form->addRow("Account:", m_tradeAccountCombo);
    form->addRow("Ticker:",  m_tickerCombo);
    form->addRow("Qty:",     m_qtySpinBox);
    form->addRow("",         btnWidget);

    auto* refreshBtn = new QPushButton("Refresh Quotes");
    refreshBtn->setFixedWidth(120);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshQuotes);

    root->addWidget(m_quotesTable);
    root->addWidget(refreshBtn, 0, Qt::AlignRight);
    root->addWidget(tradeBox);
    root->addStretch();

    return w;
}

QWidget* MainWindow::buildPortfolioTab() {
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setSpacing(8);

    m_portfolioTable = makeTable({"Ticker", "Qty", "Avg Price", "Current", "P&L"});

    auto* refreshBtn = new QPushButton("Refresh Portfolio");
    refreshBtn->setFixedWidth(130);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshPortfolio);

    root->addWidget(m_portfolioTable, 1);
    root->addWidget(refreshBtn, 0, Qt::AlignRight);

    return w;
}

QWidget* MainWindow::buildHistoryTab() {
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setSpacing(8);

    auto* filterRow = new QHBoxLayout;
    m_historyAccountCombo = new QComboBox;
    auto* refreshBtn = new QPushButton("Refresh");
    refreshBtn->setFixedWidth(80);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshHistory);
    filterRow->addWidget(new QLabel("Account:"));
    filterRow->addWidget(m_historyAccountCombo, 1);
    filterRow->addWidget(refreshBtn);

    m_historyTable = makeTable({"Timestamp", "Type", "Amount", "Balance After"});

    root->addLayout(filterRow);
    root->addWidget(m_historyTable, 1);

    return w;
}

// ─── API helper ──────────────────────────────────────────────────────────────

QJsonObject MainWindow::api(const QJsonObject& req) {
    QJsonObject r = req;
    r["token"] = m_token;
    return m_client->sendRequest(r);
}

// ─── data refresh ────────────────────────────────────────────────────────────

void MainWindow::refreshAccounts() {
    auto resp = api({{"type", "get_accounts"}});
    if (resp["status"].toString() != "ok") {
        status("Failed to load accounts: " + resp["message"].toString());
        return;
    }

    m_accounts = resp["accounts"].toArray();
    m_accountsTable->setRowCount(m_accounts.size());

    for (int i = 0; i < m_accounts.size(); ++i) {
        auto a = m_accounts[i].toObject();
        auto currency = a["currency"].toString();
        m_accountsTable->setItem(i, 0, new QTableWidgetItem(
            QString::number(a["id"].toInteger())));
        m_accountsTable->setItem(i, 1, new QTableWidgetItem(currency));
        m_accountsTable->setItem(i, 2, new QTableWidgetItem(
            fmtBalance(a["balance"].toDouble(), currency)));
    }

    repopulateAccountCombos();
}

void MainWindow::refreshQuotes() {
    auto resp = api({{"type", "get_quotes"}});
    if (resp["status"].toString() != "ok") return;

    auto quotes = resp["quotes"].toArray();
    m_quotesTable->setRowCount(quotes.size());

    for (int i = 0; i < quotes.size(); ++i) {
        auto q = quotes[i].toObject();
        m_quotesTable->setItem(i, 0, new QTableWidgetItem(q["ticker"].toString()));
        m_quotesTable->setItem(i, 1, new QTableWidgetItem(
            QString("$%1").arg(q["price"].toDouble(), 0, 'f', 2)));
    }
}

void MainWindow::refreshPortfolio() {
    auto resp = api({{"type", "get_portfolio"}});
    if (resp["status"].toString() != "ok") return;

    auto positions = resp["positions"].toArray();
    m_portfolioTable->setRowCount(positions.size());

    for (int i = 0; i < positions.size(); ++i) {
        auto p = positions[i].toObject();
        double pnl = p["pnl"].toDouble();
        bool   pos = pnl >= 0.0;

        m_portfolioTable->setItem(i, 0, new QTableWidgetItem(p["ticker"].toString()));
        m_portfolioTable->setItem(i, 1, new QTableWidgetItem(
            QString::number(p["quantity"].toInt())));
        m_portfolioTable->setItem(i, 2, new QTableWidgetItem(
            QString("$%1").arg(p["avg_price"].toDouble(), 0, 'f', 2)));
        m_portfolioTable->setItem(i, 3, new QTableWidgetItem(
            QString("$%1").arg(p["current_price"].toDouble(), 0, 'f', 2)));

        auto* pnlItem = new QTableWidgetItem(
            QString("%1$%2")
                .arg(pos ? "+" : "")
                .arg(qAbs(pnl), 0, 'f', 2));
        pnlItem->setForeground(pos ? QColor("#27ae60") : QColor("#e74c3c"));
        QFont font = pnlItem->font();
        font.setBold(true);
        pnlItem->setFont(font);
        m_portfolioTable->setItem(i, 4, pnlItem);
    }
}

void MainWindow::refreshHistory() {
    qint64 accountId = m_historyAccountCombo->currentData().toLongLong();
    if (accountId == 0) return;

    auto resp = api({{"type", "get_history"}, {"account_id", accountId}});
    if (resp["status"].toString() != "ok") return;

    auto history = resp["history"].toArray();
    m_historyTable->setRowCount(history.size());

    for (int i = 0; i < history.size(); ++i) {
        auto e = history[i].toObject();
        qint64 ts = e["timestamp"].toInteger();
        double amount = e["amount"].toDouble();
        bool   income = (amount >= 0.0);

        auto* tsItem = new QTableWidgetItem(
            QDateTime::fromSecsSinceEpoch(ts).toString("yyyy-MM-dd  hh:mm:ss"));
        auto* amtItem = new QTableWidgetItem(
            QString("%1%2").arg(income ? "+" : "").arg(amount, 0, 'f', 2));
        amtItem->setForeground(income ? QColor("#27ae60") : QColor("#e74c3c"));

        m_historyTable->setItem(i, 0, tsItem);
        m_historyTable->setItem(i, 1, new QTableWidgetItem(e["op_type"].toString()));
        m_historyTable->setItem(i, 2, amtItem);
        m_historyTable->setItem(i, 3, new QTableWidgetItem(
            QString::number(e["balance_after"].toDouble(), 'f', 2)));
    }
}

void MainWindow::repopulateAccountCombos() {
    auto fill = [this](QComboBox* combo) {
        QSignalBlocker guard(combo);
        qint64 prev = combo->currentData().toLongLong();
        combo->clear();
        int restoreIdx = -1;
        for (int i = 0; i < m_accounts.size(); ++i) {
            auto a = m_accounts[i].toObject();
            qint64 id = a["id"].toInteger();
            combo->addItem(
                QString("%1  (%2)").arg(id).arg(a["currency"].toString()),
                QVariant::fromValue(id));
            if (id == prev) restoreIdx = i;
        }
        if (restoreIdx >= 0) combo->setCurrentIndex(restoreIdx);
    };

    fill(m_selAccount);
    fill(m_transferToCombo);
    fill(m_tradeAccountCombo);
    fill(m_historyAccountCombo);
}

// ─── slots ───────────────────────────────────────────────────────────────────

void MainWindow::onTabChanged(int idx) {
    // Auto-refresh quotes only while the Market tab is visible — saves I/O
    // and avoids racing the timer against operations on other tabs.
    if (idx == 1) m_timer->start(); else m_timer->stop();

    switch (idx) {
        case 0: refreshAccounts();  break;
        case 1: refreshQuotes();    break;
        case 2: refreshPortfolio(); break;
        case 3: refreshHistory();   break;
    }
}

void MainWindow::onTimerTick() {
    refreshQuotes();
}

void MainWindow::onLogout() {
    m_timer->stop();
    api({{"type", "logout"}});
    m_client->disconnectFromServer();
    close();
}

void MainWindow::onCreateAccount() {
    auto resp = api({{"type", "create_account"},
                     {"currency", m_newCurrencyCombo->currentText()}});
    if (resp["status"].toString() == "ok") {
        status(QString("Created account %1 (%2)")
               .arg(resp["account_id"].toInteger())
               .arg(m_newCurrencyCombo->currentText()));
        refreshAccounts();
    } else {
        QMessageBox::warning(this, "Error", resp["message"].toString());
    }
}

void MainWindow::onDeposit() {
    qint64 id  = m_selAccount->currentData().toLongLong();
    double amt = m_depositAmt->value();
    if (id == 0) return;

    auto resp = api({{"type", "deposit"}, {"account_id", id}, {"amount", amt}});
    if (resp["status"].toString() == "ok") {
        status(QString("Deposited %1  →  balance: %2")
               .arg(amt, 0, 'f', 2)
               .arg(resp["new_balance"].toDouble(), 0, 'f', 2));
        refreshAccounts();
    } else {
        QMessageBox::warning(this, "Error", resp["message"].toString());
    }
}

void MainWindow::onWithdraw() {
    qint64 id  = m_selAccount->currentData().toLongLong();
    double amt = m_withdrawAmt->value();
    if (id == 0) return;

    auto resp = api({{"type", "withdraw"}, {"account_id", id}, {"amount", amt}});
    if (resp["status"].toString() == "ok") {
        status(QString("Withdrew %1  →  balance: %2")
               .arg(amt, 0, 'f', 2)
               .arg(resp["new_balance"].toDouble(), 0, 'f', 2));
        refreshAccounts();
    } else {
        QMessageBox::warning(this, "Error", resp["message"].toString());
    }
}

void MainWindow::onTransfer() {
    qint64 fromId = m_selAccount->currentData().toLongLong();
    qint64 toId   = m_transferToCombo->currentData().toLongLong();
    double amt    = m_transferAmt->value();

    if (fromId == 0 || toId == 0) return;
    if (fromId == toId) {
        QMessageBox::warning(this, "Error", "Source and destination accounts must differ.");
        return;
    }

    auto resp = api({{"type", "transfer"},
                     {"from_account", fromId},
                     {"to_account",   toId},
                     {"amount", amt}});
    if (resp["status"].toString() == "ok") {
        status(QString("Transferred %1  |  from: %2  to: %3")
               .arg(amt, 0, 'f', 2)
               .arg(resp["from_balance"].toDouble(), 0, 'f', 2)
               .arg(resp["to_balance"].toDouble(),   0, 'f', 2));
        refreshAccounts();
    } else {
        QMessageBox::warning(this, "Error", resp["message"].toString());
    }
}

void MainWindow::onBuyStock() {
    qint64 accId  = m_tradeAccountCombo->currentData().toLongLong();
    QString ticker = m_tickerCombo->currentText();
    int    qty    = m_qtySpinBox->value();
    if (accId == 0) return;

    auto resp = api({{"type", "buy_stock"},
                     {"ticker",     ticker},
                     {"quantity",   qty},
                     {"account_id", accId}});
    if (resp["status"].toString() == "ok") {
        status(QString("Bought %1 × %2  @  $%3  |  new balance: %4")
               .arg(qty).arg(ticker)
               .arg(resp["price"].toDouble(),       0, 'f', 2)
               .arg(resp["new_balance"].toDouble(),  0, 'f', 2));
        refreshAccounts();
        refreshPortfolio();
    } else {
        QMessageBox::warning(this, "Trade Failed", resp["message"].toString());
    }
}

void MainWindow::onSellStock() {
    qint64 accId  = m_tradeAccountCombo->currentData().toLongLong();
    QString ticker = m_tickerCombo->currentText();
    int    qty    = m_qtySpinBox->value();
    if (accId == 0) return;

    auto resp = api({{"type", "sell_stock"},
                     {"ticker",     ticker},
                     {"quantity",   qty},
                     {"account_id", accId}});
    if (resp["status"].toString() == "ok") {
        status(QString("Sold %1 × %2  @  $%3  |  new balance: %4")
               .arg(qty).arg(ticker)
               .arg(resp["price"].toDouble(),       0, 'f', 2)
               .arg(resp["new_balance"].toDouble(),  0, 'f', 2));
        refreshAccounts();
        refreshPortfolio();
    } else {
        QMessageBox::warning(this, "Trade Failed", resp["message"].toString());
    }
}

void MainWindow::onRefreshHistory() {
    refreshHistory();
}

// ─── helpers ─────────────────────────────────────────────────────────────────

void MainWindow::status(const QString& msg) {
    statusBar()->showMessage(msg, 6000);
}

QString MainWindow::fmtBalance(double v, const QString& currency) {
    if (currency == "USD") return QString("$%1").arg(v, 0, 'f', 2);
    if (currency == "EUR") return QString("€%1").arg(v, 0, 'f', 2);
    return QString("₽%1").arg(v, 0, 'f', 2);
}
