#!/bin/bash
# ============================================================================
# Radxa ROCK 5C I2C6 Test Script
# ============================================================================
#
# Tests the ESP32-C3FH4 I2C slave at address 0x08 on /dev/i2c-6.
#
# Usage:
#   chmod +x test_i2c.sh
#   ./test_i2c.sh              # Default: 10-cycle test
#   ./test_i2c.sh single       # Single transaction test
#   ./test_i2c.sh cycle        # 10-cycle test
#   ./test_i2c.sh stress       # 100-cycle stress test
#   ./test_i2c.sh full         # Full command set test (Example 03)
#
# Prerequisites:
#   sudo apt install -y i2c-tools
#
# Author: Damika3002
# Repository: https://github.com/Damika3002/ESP32-c3-i2c-slave-reference
# License: MIT
# ============================================================================

I2C_BUS=6
I2C_ADDR=0x08

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Counters
TOTAL=0
SUCCESS=0
FAILED=0

# ----------------------------------------------------------------------------
# Print header
# ----------------------------------------------------------------------------
print_header() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE} ESP32-C3 I2C Slave Test Script${NC}"
    echo -e "${BLUE} Bus: /dev/i2c-${I2C_BUS}  Address: ${I2C_ADDR}${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
}

# ----------------------------------------------------------------------------
# Print summary
# ----------------------------------------------------------------------------
print_summary() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE} Test Summary${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo -e " Total transactions:  $TOTAL"

    if [ "$SUCCESS" -gt 0 ]; then
        echo -e " ${GREEN}Successful:           $SUCCESS${NC}"
    else
        echo -e " Successful:           $SUCCESS"
    fi

    if [ "$FAILED" -gt 0 ]; then
        echo -e " ${RED}Failed:              $FAILED${NC}"
    else
        echo -e " Failed:              $FAILED"
    fi

    if [ "$TOTAL" -gt 0 ]; then
        RATE=$((SUCCESS * 100 / TOTAL))
        echo -e " Success rate:        ${RATE}%"
    fi

    echo -e "${BLUE}========================================${NC}"

    if [ "$FAILED" -eq 0 ]; then
        echo -e "${GREEN}All tests passed.${NC}"
    else
        echo -e "${RED}Some tests failed.${NC}"
    fi
}

# ----------------------------------------------------------------------------
# Validate a 6-byte response
# ----------------------------------------------------------------------------
validate_response() {
    local response="$1"
    local expected_cmd="$2"
    local label="$3"

    # Check if response starts with 0xa5
    if echo "$response" | grep -q "0xa5"; then
        echo -e " ${GREEN}PASS${NC} - $label: $response"
        SUCCESS=$((SUCCESS + 1))
    else
        echo -e " ${RED}FAIL${NC} - $label: $response (no 0xA5 marker)"
        FAILED=$((FAILED + 1))
    fi
    TOTAL=$((TOTAL + 1))
}

# ----------------------------------------------------------------------------
# Run a single write+read transaction
# ----------------------------------------------------------------------------
run_transaction() {
    local cmd_hex="$1"
    local cmd_name="$2"

    echo -e "${CYAN}Command 0x${cmd_hex} (${cmd_name})${NC}"

    # Write command
    sudo i2ctransfer -y "$I2C_BUS" w2@"$I2C_ADDR" 0x01 0x"$cmd_hex" 2>/dev/null
    local write_status=$?

    if [ "$write_status" -ne 0 ]; then
        echo -e " ${RED}FAIL${NC} - Write failed (No such device or address)"
        FAILED=$((FAILED + 1))
        TOTAL=$((TOTAL + 1))
        return 1
    fi

    sleep 0.50

    # Read response
    local response
    response=$(sudo i2ctransfer -y "$I2C_BUS" r6@"$I2C_ADDR" 2>/dev/null)
    local read_status=$?

    if [ "$read_status" -ne 0 ]; then
        echo -e " ${RED}FAIL${NC} - Read failed (No such device or address)"
        FAILED=$((FAILED + 1))
        TOTAL=$((TOTAL + 1))
        return 1
    fi

    validate_response "$response" "$cmd_hex" "$cmd_name"
}

# ----------------------------------------------------------------------------
# Single transaction test
# ----------------------------------------------------------------------------
test_single() {
    echo -e "${YELLOW}=== Single Transaction Test ===${NC}"
    echo ""
    run_transaction "99" "Initialize"
    echo ""
    run_transaction "2a" "Status"
}

# ----------------------------------------------------------------------------
# Cycle test (10 cycles)
# ----------------------------------------------------------------------------
test_cycle() {
    echo -e "${YELLOW}=== Cycle Test (10 cycles, 20 transactions) ===${NC}"
    echo ""

    for i in $(seq 1 10); do
        echo -e "${YELLOW}--- Cycle $i ---${NC}"
        run_transaction "99" "Initialize"
        run_transaction "2a" "Status"
        sleep 0.30
        echo ""
    done
}

# ----------------------------------------------------------------------------
# Stress test (100 cycles)
# ----------------------------------------------------------------------------
test_stress() {
    echo -e "${YELLOW}=== Stress Test (100 cycles, 200 transactions) ===${NC}"
    echo ""

    for i in $(seq 1 100); do
        if [ $((i % 10)) -eq 0 ]; then
            echo -e "${YELLOW}--- Cycle $i ---${NC}"
        fi

        run_transaction "99" "Initialize"
        run_transaction "2a" "Status"

        if [ $((i % 10)) -eq 0 ]; then
            echo "  Running total: $SUCCESS / $TOTAL passed"
        fi

        sleep 0.10
    done
}

# ----------------------------------------------------------------------------
# Full command set test (for Example 03 Fan Controller)
# ----------------------------------------------------------------------------
test_full() {
    echo -e "${YELLOW}=== Full Command Set Test (Fan Controller) ===${NC}"
    echo ""
    echo -e "${CYAN}Note: Requires Example 03 firmware loaded on ESP32-C3${NC}"
    echo ""

    run_transaction "99" "Initialize/Heartbeat"
    run_transaction "2a" "Status Request"
    run_transaction "10" "Read Temperature"
    run_transaction "11" "Read Fan Speed"
    run_transaction "20" "Fan Override ON"
    sleep 1.00
    run_transaction "2a" "Status (after override)"
    run_transaction "21" "Fan Override OFF"
    run_transaction "2a" "Status (after clear)"
    run_transaction "30" "Read eFuse Status"
    run_transaction "40" "Read Error Counters"
}

# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------
print_header

case "${1:-cycle}" in
    single)
        test_single
        ;;
    cycle)
        test_cycle
        ;;
    stress)
        test_stress
        ;;
    full)
        test_full
        ;;
    *)
        echo "Usage: $0 {single|cycle|stress|full}"
        echo ""
        echo "  single  - Single transaction test (2 commands)"
        echo "  cycle   - 10-cycle test (20 transactions, default)"
        echo "  stress  - 100-cycle stress test (200 transactions)"
        echo "  full    - Full command set test (Fan Controller firmware)"
        exit 1
        ;;
esac

print_summary

if [ "$FAILED" -eq 0 ]; then
    exit 0
else
    exit 1
fi
