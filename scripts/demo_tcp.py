#!/usr/bin/env python3
import json
import socket
import struct
import sys


def send_request(sock: socket.socket, payload: dict) -> dict:
    body = json.dumps(payload).encode("utf-8")
    sock.sendall(struct.pack(">I", len(body)) + body)

    header = recv_exact(sock, 4)
    if not header:
        raise RuntimeError("No response header")
    (size,) = struct.unpack(">I", header)
    if size <= 0 or size > 1024 * 1024:
        raise RuntimeError(f"Invalid response frame size: {size}")

    response_body = recv_exact(sock, size)
    if not response_body:
        raise RuntimeError("No response body")
    return json.loads(response_body.decode("utf-8"))


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            return b""
        data.extend(chunk)
    return bytes(data)


def check_ok(step: str, response: dict) -> None:
    if response.get("status") != "ok":
        raise RuntimeError(f"{step} failed: {response}")
    print(f"[OK] {step}: {response}")


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9090

    with socket.create_connection((host, port), timeout=5) as sock:
        register_resp = send_request(sock, {
            "type": "register",
            "username": "demo_user",
            "password": "demo_pass"
        })
        if register_resp.get("status") == "error":
            print(f"[INFO] register returned error (possibly already exists): {register_resp}")
        else:
            check_ok("register", register_resp)

        login_resp = send_request(sock, {
            "type": "login",
            "username": "demo_user",
            "password": "demo_pass"
        })
        check_ok("login", login_resp)
        token = login_resp["token"]

        create_a = send_request(sock, {
            "type": "create_account",
            "token": token,
            "currency": "RUB"
        })
        check_ok("create_account RUB", create_a)
        acc_rub = create_a["account_id"]

        create_b = send_request(sock, {
            "type": "create_account",
            "token": token,
            "currency": "USD"
        })
        check_ok("create_account USD", create_b)
        acc_usd = create_b["account_id"]

        dep = send_request(sock, {
            "type": "deposit",
            "token": token,
            "account_id": acc_rub,
            "amount": 10000.0
        })
        check_ok("deposit RUB", dep)

        tr = send_request(sock, {
            "type": "transfer",
            "token": token,
            "from_account": acc_rub,
            "to_account": acc_usd,
            "amount": 9250.0
        })
        check_ok("transfer RUB->USD", tr)

        accounts = send_request(sock, {
            "type": "get_accounts",
            "token": token
        })
        check_ok("get_accounts", accounts)

        logout = send_request(sock, {
            "type": "logout",
            "token": token
        })
        check_ok("logout", logout)

    print("\nDemo scenario finished successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
