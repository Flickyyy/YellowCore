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
#include <QFrame>
#include <QShortcut>
#include <QKeySequence>
#include <QtMath>
#include <algorithm>

// ─── constructor ─────────────────────────────────────────────────────────────

MainWindow::MainWindow(TcpClient* client,
                       const QString& token,
                       const QString& username,
                       QWidget* parent)
    : QMainWindow(parent), m_client(client), m_token(token), m_username(username)
{
    setWindowTitle(QString("YellowCore — %1").arg(username));
    resize(1180, 820);
    setMinimumSize(1000, 700);

    auto* central = new QWidget;
    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(8, 8, 8, 0);
    vbox->setSpacing(6);

    // Stats header — eye-candy bar with live totals
    auto* statsBar = new QFrame;
    statsBar->setObjectName("statsBar");
    statsBar->setStyleSheet(
        "QFrame#statsBar{"
        "  background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "    stop:0 #2c3e50, stop:1 #34495e);"
        "  border-radius:8px;"
        "  padding:10px 14px;"
        "}"
        "QFrame#statsBar QLabel{color:#ecf0f1;}"
        "QLabel#statsTitle{font-weight:600;font-size:11pt;color:#bdc3c7;}"
        "QLabel#statsValue{font-weight:700;font-size:14pt;color:#ffffff;}"
        "QLabel#statsValuePos{font-weight:700;font-size:14pt;color:#2ecc71;}"
        "QLabel#statsValueNeg{font-weight:700;font-size:14pt;color:#e74c3c;}");
    auto* statsLayout = new QHBoxLayout(statsBar);
    statsLayout->setContentsMargins(14, 10, 14, 10);

    auto makeStatColumn = [](const QString& title, QLabel*& valueRef,
                             const QString& objectName = "statsValue") {
        auto* col = new QVBoxLayout;
        col->setSpacing(2);
        auto* t = new QLabel(title);
        t->setObjectName("statsTitle");
        valueRef = new QLabel("—");
        valueRef->setObjectName(objectName);
        col->addWidget(t);
        col->addWidget(valueRef);
        auto* w = new QWidget;
        w->setLayout(col);
        return w;
    };

    auto* userCol = new QVBoxLayout;
    userCol->setSpacing(2);
    auto* userTitle = new QLabel("USER");
    userTitle->setObjectName("statsTitle");
    auto* userVal = new QLabel(username);
    userVal->setObjectName("statsValue");
    userCol->addWidget(userTitle);
    userCol->addWidget(userVal);
    auto* userColW = new QWidget;
    userColW->setLayout(userCol);

    auto* sep1 = new QFrame; sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet("color:#7f8c8d;");
    auto* sep2 = new QFrame; sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet("color:#7f8c8d;");
    auto* sep3 = new QFrame; sep3->setFrameShape(QFrame::VLine);
    sep3->setStyleSheet("color:#7f8c8d;");

    statsLayout->addWidget(userColW);
    statsLayout->addWidget(sep1);
    statsLayout->addWidget(makeStatColumn("CASH BALANCES", m_statsCash));
    statsLayout->addWidget(sep2);
    statsLayout->addWidget(makeStatColumn("PORTFOLIO VALUE", m_statsPortfolio));
    statsLayout->addWidget(sep3);
    statsLayout->addWidget(makeStatColumn("UNREALIZED P&L", m_statsPnL));
    statsLayout->addStretch();

    auto* logoutBtn = new QPushButton("⏻  Logout");
    logoutBtn->setFixedWidth(110);
    logoutBtn->setStyleSheet(
        "QPushButton{background:#e74c3c;border:none;color:white;"
        "font-weight:600;border-radius:4px;padding:8px 12px;}"
        "QPushButton:hover{background:#c0392b;}");
    statsLayout->addWidget(logoutBtn);

    vbox->addWidget(statsBar);

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
    refreshExchangeRates();
    // Timer auto-runs only while Market or Portfolio tab is visible; see onTabChanged.

    // F5 — refresh whichever tab is active.
    auto* refreshShortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(refreshShortcut, &QShortcut::activated, this, [this]() {
        onTabChanged(m_currentTab);
    });
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
    t->setSortingEnabled(true);
    t->horizontalHeader()->setSectionsClickable(true);
    return t;
}

QWidget* MainWindow::buildAccountsTab() {
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setSpacing(8);

    m_accountsTable = makeTable({"Account ID", "Currency", "Balance"});

    auto* refreshBtn = new QPushButton("Refresh");
    refreshBtn->setFixedWidth(80);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshAccounts);

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

    auto* depBtn   = new QPushButton("Deposit");
    auto* wdBtn    = new QPushButton("Withdraw");
    auto* xfrBtn   = new QPushButton("Transfer");
    auto* closeBtn = new QPushButton("Close Account");
    depBtn->setFixedWidth(80);
    wdBtn->setFixedWidth(80);
    xfrBtn->setFixedWidth(80);
    closeBtn->setFixedWidth(120);
    closeBtn->setStyleSheet(
        "QPushButton{background:#fff3f3;border-color:#e6b1b1;color:#c0392b;}"
        "QPushButton:hover{background:#ffe5e5;border-color:#c0392b;}");
    connect(depBtn,   &QPushButton::clicked, this, &MainWindow::onDeposit);
    connect(wdBtn,    &QPushButton::clicked, this, &MainWindow::onWithdraw);
    connect(xfrBtn,   &QPushButton::clicked, this, &MainWindow::onTransfer);
    connect(closeBtn, &QPushButton::clicked, this, &MainWindow::onCloseAccount);

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

    auto* selRow = new QHBoxLayout;
    selRow->addWidget(m_selAccount, 1);
    selRow->addWidget(closeBtn);
    auto* selWidget = new QWidget;
    selWidget->setLayout(selRow);

    opsForm->addRow("Account:", selWidget);
    opsForm->addRow("Deposit:",  makeRow(m_depositAmt,  depBtn));
    opsForm->addRow("Withdraw:", makeRow(m_withdrawAmt, wdBtn));
    opsForm->addRow("Transfer:", xfrWidget);

    // Exchange rates panel — full-height column on the right side
    auto* ratesBox = new QGroupBox("Live Exchange Rates");
    auto* ratesLayout = new QVBoxLayout(ratesBox);
    m_ratesTable = makeTable({"Pair", "Rate", "Change"});
    m_ratesTable->setMinimumHeight(220);
    ratesLayout->addWidget(m_ratesTable);

    // Left column: accounts table + refresh + create + operations
    auto* leftCol = new QVBoxLayout;
    leftCol->setSpacing(8);
    m_accountsTable->setMinimumHeight(180);
    leftCol->addWidget(m_accountsTable, 1);
    leftCol->addWidget(refreshBtn, 0, Qt::AlignRight);
    leftCol->addWidget(createBox);
    leftCol->addWidget(opsBox);
    auto* leftWidget = new QWidget;
    leftWidget->setLayout(leftCol);

    // Two-column layout: accounts/ops on the left, rates on the right
    auto* split = new QHBoxLayout;
    split->setSpacing(10);
    split->addWidget(leftWidget, 3);
    split->addWidget(ratesBox,   2);

    root->addLayout(split);

    return w;
}

QWidget* MainWindow::buildMarketTab() {
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setSpacing(8);

    m_quotesTable = makeTable({"Ticker", "Price (USD)", "Change"});
    m_quotesTable->setFixedHeight(260);

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

    auto* posLbl = new QLabel("<b>Open Positions</b>");
    m_portfolioTable = makeTable({"Ticker", "Qty", "Avg Price", "Current", "P&L"});

    auto* trdLbl = new QLabel("<b>Trade Log</b>");
    m_tradesTable = makeTable({"Timestamp", "Ticker", "Side", "Qty", "Price"});

    auto* refreshBtn = new QPushButton("Refresh");
    refreshBtn->setFixedWidth(100);
    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        refreshPortfolio();
        refreshTrades();
    });

    root->addWidget(posLbl);
    root->addWidget(m_portfolioTable, 1);
    root->addWidget(trdLbl);
    root->addWidget(m_tradesTable, 1);
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
    m_accountsTable->setSortingEnabled(false);

    if (m_accounts.isEmpty()) {
        // Empty-state row spanning full width
        m_accountsTable->setRowCount(1);
        auto* hint = new QTableWidgetItem(
            "No accounts yet. Use \"New Account\" below to create one.");
        hint->setTextAlignment(Qt::AlignCenter);
        hint->setForeground(QColor("#7f8c8d"));
        QFont f = hint->font(); f.setItalic(true); hint->setFont(f);
        m_accountsTable->setItem(0, 0, hint);
        m_accountsTable->setSpan(0, 0, 1, m_accountsTable->columnCount());
    } else {
        m_accountsTable->clearSpans();
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
    }
    m_accountsTable->setSortingEnabled(true);

    repopulateAccountCombos();
    updateStatsHeader();
}

void MainWindow::refreshQuotes() {
    auto resp = api({{"type", "get_quotes"}});
    if (resp["status"].toString() != "ok") return;

    auto quotes = resp["quotes"].toArray();
    m_quotesTable->setSortingEnabled(false);  // suspend sorting during update
    m_quotesTable->setRowCount(quotes.size());

    for (int i = 0; i < quotes.size(); ++i) {
        auto q = quotes[i].toObject();
        QString ticker = q["ticker"].toString();
        double  price  = q["price"].toDouble();

        // Compare to last seen price for ▲/▼ indicator
        double prev   = m_lastQuotes.value(ticker, price);
        double delta  = price - prev;
        double pct    = prev > 0.0 ? (delta / prev) * 100.0 : 0.0;
        m_lastQuotes[ticker] = price;

        m_quotesTable->setItem(i, 0, new QTableWidgetItem(ticker));

        auto* priceItem = new QTableWidgetItem(
            QString("$%1").arg(price, 0, 'f', 2));
        QFont f = priceItem->font(); f.setBold(true); priceItem->setFont(f);
        m_quotesTable->setItem(i, 1, priceItem);

        QString arrow, deltaStr;
        QColor color;
        if (qAbs(delta) < 1e-9) {
            arrow = "—"; deltaStr = "0.00%"; color = QColor("#7f8c8d");
        } else if (delta > 0) {
            arrow = "▲";
            deltaStr = QString("+%1%").arg(pct, 0, 'f', 2);
            color = QColor("#27ae60");
        } else {
            arrow = "▼";
            deltaStr = QString("%1%").arg(pct, 0, 'f', 2);
            color = QColor("#c0392b");
        }
        auto* changeItem = new QTableWidgetItem(QString("%1  %2").arg(arrow, deltaStr));
        changeItem->setForeground(color);
        QFont cf = changeItem->font(); cf.setBold(true); changeItem->setFont(cf);
        m_quotesTable->setItem(i, 2, changeItem);
    }
    m_quotesTable->setSortingEnabled(true);
}

void MainWindow::refreshPortfolio() {
    auto resp = api({{"type", "get_portfolio"}});
    if (resp["status"].toString() != "ok") return;

    auto positions = resp["positions"].toArray();
    m_portfolioTable->setSortingEnabled(false);
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
    m_portfolioTable->setSortingEnabled(true);
    updateStatsHeader();
}

void MainWindow::refreshHistory() {
    qint64 accountId = m_historyAccountCombo->currentData().toLongLong();
    if (accountId == 0) return;

    auto resp = api({{"type", "get_history"}, {"account_id", accountId}});
    if (resp["status"].toString() != "ok") return;

    auto history = resp["history"].toArray();
    m_historyTable->setSortingEnabled(false);
    m_historyTable->setRowCount(history.size());

    for (int i = 0; i < history.size(); ++i) {
        auto e = history[i].toObject();
        qint64 ts = e["timestamp"].toInteger();
        double amount = e["amount"].toDouble();
        QString op = e["op_type"].toString();

        // Server always returns positive amount; income/expense is encoded
        // by op_type: deposit/transfer_in/sell_stock = income, others = expense.
        const bool income = (op == "deposit" || op == "transfer_in" || op == "sell_stock");

        auto* tsItem = new QTableWidgetItem(
            QDateTime::fromSecsSinceEpoch(ts).toString("yyyy-MM-dd  hh:mm:ss"));
        auto* amtItem = new QTableWidgetItem(
            QString("%1%2").arg(income ? "+" : "−").arg(amount, 0, 'f', 2));
        amtItem->setForeground(income ? QColor("#27ae60") : QColor("#e74c3c"));

        m_historyTable->setItem(i, 0, tsItem);
        m_historyTable->setItem(i, 1, new QTableWidgetItem(op));
        m_historyTable->setItem(i, 2, amtItem);
        m_historyTable->setItem(i, 3, new QTableWidgetItem(
            QString::number(e["balance_after"].toDouble(), 'f', 2)));
    }
    m_historyTable->setSortingEnabled(true);
}

void MainWindow::updateStatsHeader() {
    // Cash totals per currency
    QMap<QString, double> totals;
    for (const auto& v : m_accounts) {
        auto a = v.toObject();
        totals[a["currency"].toString()] += a["balance"].toDouble();
    }

    QStringList parts;
    auto fmt = [](double x) { return QString::number(x, 'f', 2); };
    if (totals.contains("USD")) parts << QString("$%1").arg(fmt(totals["USD"]));
    if (totals.contains("EUR")) parts << QString("€%1").arg(fmt(totals["EUR"]));
    if (totals.contains("RUB")) parts << QString("₽%1").arg(fmt(totals["RUB"]));
    m_statsCash->setText(parts.isEmpty() ? "no accounts yet"
                                         : parts.join("   ·   "));

    // Portfolio market value + unrealized P&L (we'll fetch lazily, but if we
    // already have a portfolio table populated, sum from there).
    auto resp = api({{"type", "get_portfolio"}});
    if (resp["status"].toString() == "ok") {
        auto positions = resp["positions"].toArray();
        double mv = 0.0, pnl = 0.0;
        for (const auto& p : positions) {
            auto o = p.toObject();
            mv  += o["quantity"].toInt() * o["current_price"].toDouble();
            pnl += o["pnl"].toDouble();
        }
        m_statsPortfolio->setText(positions.isEmpty()
            ? QString("—")
            : QString("$%1").arg(QString::number(mv, 'f', 2)));

        if (positions.isEmpty()) {
            m_statsPnL->setText("—");
            m_statsPnL->setStyleSheet("font-weight:700;font-size:14pt;color:#ffffff;");
        } else {
            QString text = QString("%1$%2").arg(pnl >= 0 ? "+" : "−")
                                            .arg(qAbs(pnl), 0, 'f', 2);
            m_statsPnL->setText(text);
            m_statsPnL->setStyleSheet(
                QString("font-weight:700;font-size:14pt;color:%1;")
                    .arg(pnl >= 0 ? "#2ecc71" : "#e74c3c"));
        }
    }
}

void MainWindow::refreshExchangeRates() {
    auto resp = api({{"type", "get_exchange_rates"}});
    if (resp["status"].toString() != "ok") return;

    auto rates = resp["rates"].toObject();
    m_ratesTable->setSortingEnabled(false);
    m_ratesTable->setRowCount(rates.size());

    int row = 0;
    auto keys = rates.keys();
    std::sort(keys.begin(), keys.end());
    for (const auto& pair : keys) {
        double  rate = rates[pair].toDouble();
        double  prev = m_lastRates.value(pair, rate);
        double  delta = rate - prev;
        double  pct = prev > 0.0 ? (delta / prev) * 100.0 : 0.0;
        m_lastRates[pair] = rate;

        QString readable = pair;
        readable.replace('_', " → ");
        m_ratesTable->setItem(row, 0, new QTableWidgetItem(readable));
        m_ratesTable->setItem(row, 1, new QTableWidgetItem(
            QString::number(rate, 'f', 4)));

        QString arrow, deltaStr;
        QColor color;
        if (qAbs(delta) < 1e-9) {
            arrow = "—"; deltaStr = "0.00%"; color = QColor("#7f8c8d");
        } else if (delta > 0) {
            arrow = "▲";
            deltaStr = QString("+%1%").arg(pct, 0, 'f', 3);
            color = QColor("#27ae60");
        } else {
            arrow = "▼";
            deltaStr = QString("%1%").arg(pct, 0, 'f', 3);
            color = QColor("#c0392b");
        }
        auto* changeItem = new QTableWidgetItem(QString("%1  %2").arg(arrow, deltaStr));
        changeItem->setForeground(color);
        QFont cf = changeItem->font(); cf.setBold(true); changeItem->setFont(cf);
        m_ratesTable->setItem(row, 2, changeItem);
        ++row;
    }
    m_ratesTable->setSortingEnabled(true);
}

void MainWindow::refreshTrades() {
    auto resp = api({{"type", "get_trades"}});
    if (resp["status"].toString() != "ok") return;

    auto trades = resp["trades"].toArray();
    m_tradesTable->setSortingEnabled(false);
    m_tradesTable->setRowCount(trades.size());

    for (int i = 0; i < trades.size(); ++i) {
        auto t = trades[i].toObject();
        qint64 ts   = t["timestamp"].toInteger();
        QString side = t["side"].toString();
        bool isBuy   = (side == "buy");

        m_tradesTable->setItem(i, 0, new QTableWidgetItem(
            QDateTime::fromSecsSinceEpoch(ts).toString("yyyy-MM-dd  hh:mm:ss")));
        m_tradesTable->setItem(i, 1, new QTableWidgetItem(t["ticker"].toString()));

        auto* sideItem = new QTableWidgetItem(side.toUpper());
        sideItem->setForeground(isBuy ? QColor("#27ae60") : QColor("#c0392b"));
        QFont f = sideItem->font(); f.setBold(true); sideItem->setFont(f);
        m_tradesTable->setItem(i, 2, sideItem);

        m_tradesTable->setItem(i, 3, new QTableWidgetItem(
            QString::number(t["quantity"].toInt())));
        m_tradesTable->setItem(i, 4, new QTableWidgetItem(
            QString("$%1").arg(t["price"].toDouble(), 0, 'f', 2)));
    }
    m_tradesTable->setSortingEnabled(true);
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
    m_currentTab = idx;
    // Timer is always running so the stats header (cash / portfolio / P&L)
    // stays live regardless of the selected tab. Per-tab data is refreshed
    // inside onTimerTick() based on which tab is visible.
    m_timer->start();

    switch (idx) {
        case 0:
            refreshAccounts();
            refreshExchangeRates();
            break;
        case 1:
            refreshQuotes();
            break;
        case 2:
            refreshPortfolio();
            refreshTrades();
            break;
        case 3:
            refreshHistory();
            break;
    }
}

void MainWindow::onTimerTick() {
    // Stats header (cash / portfolio / P&L) always refreshes — independent of tab.
    if (m_currentTab == 0) {
        refreshExchangeRates();   // FX rates jitter ±0.5% per server tick
        updateStatsHeader();
    } else if (m_currentTab == 1) {
        refreshQuotes();          // stock prices
        updateStatsHeader();      // unrealized P&L moves with quote prices
    } else if (m_currentTab == 2) {
        refreshPortfolio();       // already calls updateStatsHeader internally
    } else {
        updateStatsHeader();      // History tab: at least keep the header live
    }
}

void MainWindow::onLogout() {
    auto answer = QMessageBox::question(this, "Logout",
        "Disconnect and exit?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

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

void MainWindow::onCloseAccount() {
    qint64 id = m_selAccount->currentData().toLongLong();
    if (id == 0) return;

    auto answer = QMessageBox::question(this, "Close Account",
        QString("Close account %1?\n\nThe account must have zero balance and "
                "no open stock positions.").arg(id),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    auto resp = api({{"type", "close_account"}, {"account_id", id}});
    if (resp["status"].toString() == "ok") {
        status(QString("Closed account %1").arg(id));
        refreshAccounts();
    } else {
        QMessageBox::warning(this, "Cannot Close", resp["message"].toString());
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
