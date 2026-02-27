#pragma once

// ============================================
// YellowCore — Протокол обмена (JSON over TCP)
// ============================================
//
// Framing: [4 bytes length (big-endian)] [JSON payload]
//
// Каждое сообщение — JSON-объект. Клиент отправляет запрос,
// сервер отвечает. Сервер также может отправить push-уведомление.
//
// ------- АУТЕНТИФИКАЦИЯ -------
//
// >> {"type": "register", "username": "john", "password": "secret"}
// << {"status": "ok"} | {"status": "error", "message": "Username taken"}
//
// >> {"type": "login", "username": "john", "password": "secret"}
// << {"status": "ok", "token": "abc123"} | {"status": "error", "message": "Invalid credentials"}
//
// >> {"type": "logout", "token": "abc123"}
// << {"status": "ok"}
//
// ------- СЧЕТА -------
//
// >> {"type": "create_account", "token": "abc123", "currency": "RUB"}
// << {"status": "ok", "account_id": 100001}
//
// >> {"type": "get_accounts", "token": "abc123"}
// << {"status": "ok", "accounts": [{"id": 100001, "currency": "RUB", "balance": 5000.0}, ...]}
//
// >> {"type": "close_account", "token": "abc123", "account_id": 100001}
// << {"status": "ok"} | {"status": "error", "message": "Balance not zero"}
//
// ------- ОПЕРАЦИИ -------
//
// >> {"type": "deposit", "token": "abc123", "account_id": 100001, "amount": 1000.0}
// << {"status": "ok", "new_balance": 6000.0}
//
// >> {"type": "withdraw", "token": "abc123", "account_id": 100001, "amount": 500.0}
// << {"status": "ok", "new_balance": 5500.0}
//
// >> {"type": "transfer", "token": "abc123", "from_account": 100001, "to_account": 100002, "amount": 200.0}
// << {"status": "ok", "from_balance": 5300.0, "to_balance": 200.0}
// (при разных валютах — конвертация по текущему курсу, ответ содержит "converted_amount")
//
// ------- ИСТОРИЯ -------
//
// >> {"type": "get_history", "token": "abc123", "account_id": 100001, "filter_type": "all", "from_date": "...", "to_date": "..."}
// << {"status": "ok", "history": [{"timestamp": "...", "op_type": "deposit", "amount": 1000.0, "balance_after": 6000.0, "counterparty": null}, ...]}
//
// ------- КУРСЫ ВАЛЮТ -------
//
// >> {"type": "get_exchange_rates", "token": "abc123"}
// << {"status": "ok", "rates": {"USD_RUB": 92.5, "EUR_RUB": 100.3, "USD_EUR": 0.92, ...}}
//
// ------- ТОРГОВЛЯ АКЦИЯМИ -------
//
// >> {"type": "get_quotes", "token": "abc123"}
// << {"status": "ok", "quotes": [{"ticker": "AAPL", "price": 178.50}, {"ticker": "GOOGL", "price": 140.20}, ...]}
//
// >> {"type": "buy_stock", "token": "abc123", "ticker": "AAPL", "quantity": 10, "account_id": 100001}
// << {"status": "ok", "price": 178.50, "total_cost": 1785.0, "new_balance": 3515.0}
//
// >> {"type": "sell_stock", "token": "abc123", "ticker": "AAPL", "quantity": 5, "account_id": 100001}
// << {"status": "ok", "price": 180.00, "total_revenue": 900.0, "new_balance": 4415.0}
//
// >> {"type": "get_portfolio", "token": "abc123"}
// << {"status": "ok", "positions": [{"ticker": "AAPL", "quantity": 5, "avg_price": 178.50, "current_price": 180.00, "pnl": 7.50}, ...]}
//
// >> {"type": "get_trades", "token": "abc123"}
// << {"status": "ok", "trades": [{"timestamp": "...", "ticker": "AAPL", "side": "buy", "quantity": 10, "price": 178.50}, ...]}
//
// ------- PUSH-УВЕДОМЛЕНИЯ (сервер → клиент) -------
//
// << {"type": "notification", "event": "price_alert", "ticker": "TSLA", "old_price": 200.0, "new_price": 212.0, "change_pct": 6.0}
// << {"type": "notification", "event": "incoming_transfer", "from_user": "alice", "amount": 500.0, "currency": "RUB", "account_id": 100001}
//
// ------- ОБЩИЕ ОШИБКИ -------
//
// << {"status": "error", "message": "Invalid token"}
// << {"status": "error", "message": "Account not found"}
// << {"status": "error", "message": "Insufficient funds"}
