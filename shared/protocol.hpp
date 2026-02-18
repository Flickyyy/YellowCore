#pragma once

// Протокол обмена данными (JSON+HTTP \ gRPC)
// Запросы от клиента к серверу:

// {"type": "login", "username": "john", "password": "secret"}
// {"type": "get_accounts"}
// {"type": "get_balance", "account_id": 1}
// {"type": "transfer", "from": 1, "to": 2, "amount": 100}
// {"type": "get_history", "account_id": 1}

// Ответы сервера:

// {"status": "success", "data": {...}}
// {"status": "error", "message": "Invalid credentials"}
// {"status": "pending"}
