#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTimer>
#include <QJsonArray>
#include "TcpClient.hpp"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(TcpClient* client,
               const QString& token,
               const QString& username,
               QWidget* parent = nullptr);

private slots:
    void onTabChanged(int idx);
    void onTimerTick();
    void onLogout();

    // Accounts tab
    void onCreateAccount();
    void onDeposit();
    void onWithdraw();
    void onTransfer();

    // Market tab
    void onBuyStock();
    void onSellStock();

    // History tab
    void onRefreshHistory();

private:
    TcpClient* m_client;
    QString    m_token;
    QString    m_username;
    QTimer*    m_timer;

    QJsonArray m_accounts; // cached after each refresh

    // ---- Accounts tab ----
    QWidget*       buildAccountsTab();
    QTableWidget*  m_accountsTable;
    QComboBox*     m_newCurrencyCombo;
    QComboBox*     m_selAccount;       // source for deposit/withdraw/transfer-from
    QDoubleSpinBox* m_depositAmt;
    QDoubleSpinBox* m_withdrawAmt;
    QComboBox*     m_transferToCombo;
    QDoubleSpinBox* m_transferAmt;

    // ---- Market tab ----
    QWidget*      buildMarketTab();
    QTableWidget* m_quotesTable;
    QComboBox*    m_tradeAccountCombo;
    QComboBox*    m_tickerCombo;
    QSpinBox*     m_qtySpinBox;

    // ---- Portfolio tab ----
    QWidget*      buildPortfolioTab();
    QTableWidget* m_portfolioTable;

    // ---- History tab ----
    QWidget*      buildHistoryTab();
    QComboBox*    m_historyAccountCombo;
    QTableWidget* m_historyTable;

    // ---- helpers ----
    QJsonObject api(const QJsonObject& req);  // auto-injects token
    void refreshAccounts();
    void refreshQuotes();
    void refreshPortfolio();
    void refreshHistory();
    void repopulateAccountCombos();
    void status(const QString& msg);
    static QString fmtBalance(double v, const QString& currency);
};
