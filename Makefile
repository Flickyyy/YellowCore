# YellowCore — convenience wrapper around CMake/CTest.
# Underlying build still uses CMake; this just wraps the common workflows.

BUILD_DIR    ?= build
BUILD_TYPE   ?= Release
JOBS         ?= $(shell nproc 2>/dev/null || echo 4)
HOST         ?= 0.0.0.0
PORT         ?= 9090
WORKERS      ?= 4
SERVER_LOG   ?= /tmp/yc_server.log
SERVER_PID   ?= /tmp/yc_server.pid

SERVER_BIN   := $(BUILD_DIR)/server/yellowcore_server
CLIENT_BIN   := $(BUILD_DIR)/client/yellowcore_client

# Use offscreen Qt platform when running tests headless (CI / no-display)
TEST_ENV     := QT_QPA_PLATFORM=offscreen

.PHONY: help build configure rebuild clean \
        server server-bg server-stop \
        client run \
        test test-e2e \
        format check status

help:
	@echo "YellowCore — make targets:"
	@echo ""
	@echo "  build         Configure (if needed) and compile everything"
	@echo "  rebuild       Clean and rebuild from scratch"
	@echo "  clean         Remove $(BUILD_DIR)/"
	@echo ""
	@echo "  server        Run the server in the foreground (Ctrl-C to stop)"
	@echo "  server-bg     Run the server detached, log → $(SERVER_LOG)"
	@echo "  server-stop   Stop a backgrounded server"
	@echo "  client        Launch the Qt client (server must be running)"
	@echo "  run           server-bg, then client; stops the server on exit"
	@echo ""
	@echo "  test          Run all tests headless (server + client unit tests)"
	@echo "  test-e2e      Like 'test' but also runs the e2e case against a"
	@echo "                live server (started + stopped automatically)"
	@echo ""
	@echo "  status        Show server PID, port, recent log tail"
	@echo ""
	@echo "Vars: BUILD_DIR=$(BUILD_DIR)  BUILD_TYPE=$(BUILD_TYPE)  PORT=$(PORT)  JOBS=$(JOBS)"

# ---- build --------------------------------------------------------------

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

rebuild: clean build

clean:
	rm -rf $(BUILD_DIR)

# ---- server -------------------------------------------------------------

server: build
	$(SERVER_BIN) $(HOST) $(PORT) $(WORKERS)

server-bg: build server-stop
	@nohup $(SERVER_BIN) $(HOST) $(PORT) $(WORKERS) > $(SERVER_LOG) 2>&1 & \
		echo $$! > $(SERVER_PID)
	@sleep 1
	@echo "server pid=$$(cat $(SERVER_PID))  port=$(PORT)  log=$(SERVER_LOG)"

server-stop:
	-@if [ -f $(SERVER_PID) ]; then \
		kill $$(cat $(SERVER_PID)) 2>/dev/null ; \
		rm -f $(SERVER_PID) ; \
	fi
	-@pkill -f yellowcore_server 2>/dev/null
	-@sleep 0.3
	@true

# ---- client -------------------------------------------------------------

client: build
	$(CLIENT_BIN)

# Bring up server in the background, run the GUI, then take the server down.
run: build
	@$(MAKE) -s server-bg
	@$(CLIENT_BIN) || true
	@$(MAKE) -s server-stop

# ---- tests --------------------------------------------------------------

test: build
	$(TEST_ENV) ctest --test-dir $(BUILD_DIR) --output-on-failure -j$(JOBS)

# Same as `test`, but also runs the e2e case (which talks to a live server).
test-e2e: build
	@$(MAKE) -s server-bg
	@set -e ; \
	  $(TEST_ENV) YELLOWCORE_E2E=1 ctest --test-dir $(BUILD_DIR) \
	      --output-on-failure -j$(JOBS) ; \
	  status=$$? ; \
	  $(MAKE) -s server-stop ; \
	  exit $$status

# ---- diagnostics --------------------------------------------------------

status:
	@echo "── processes ──"
	@pgrep -af yellowcore_server | grep -v zsh || echo "(server not running)"
	@pgrep -af yellowcore_client | grep -v zsh || echo "(client not running)"
	@echo "── port $(PORT) ──"
	@ss -tlnp 2>/dev/null | grep ":$(PORT)" || echo "(port closed)"
	@echo "── server log tail ──"
	@tail -n 5 $(SERVER_LOG) 2>/dev/null || echo "(no log)"
