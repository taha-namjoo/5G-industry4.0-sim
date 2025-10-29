#!/bin/bash

# Automated Experiment Runner (Enhanced for Long-Running xApp)
# Runs traffic generation and xApp monitoring with extended xApp duration

# Color codes for better output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
TRAFFIC_SCRIPT="./traffic_gen_bursty.sh"
XAPP_BINARY="./last/last2/flexric/build/examples/xApp/c/kpm_rc/xapp_kpm_rc"
DELAY_SECONDS=0
EXPERIMENT_LOG="./experiment_run.log"
XAPP_EXTRA_DURATION=300  # ثانیه اضافی برای xApp بعد traffic_gen (مثلاً 300s = 300 نمونه)

# Create log file
echo "========================================" > $EXPERIMENT_LOG
echo "Experiment Started: $(date)" >> $EXPERIMENT_LOG
echo "XApp Extra Duration: ${XAPP_EXTRA_DURATION}s" >> $EXPERIMENT_LOG
echo "========================================" >> $EXPERIMENT_LOG

# Function to print colored messages
print_msg() {
    local color=$1
    local message=$2
    echo -e "${color}${message}${NC}"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $message" >> $EXPERIMENT_LOG
}

# Function to check if a command exists
check_file() {
    local file=$1
    if [ ! -f "$file" ]; then
        print_msg "$RED" "ERROR: File not found: $file"
        exit 1
    fi
}

# Function to cleanup on exit (بهینه‌شده: فقط kill اگر لازم)
cleanup() {
    print_msg "$YELLOW" "\n=== Cleaning up ==="
    
    # Kill traffic generation if still running
    if [ ! -z "$TRAFFIC_PID" ] && kill -0 $TRAFFIC_PID 2>/dev/null; then
        print_msg "$YELLOW" "Stopping traffic generation (PID: $TRAFFIC_PID)..."
        sudo kill -TERM $TRAFFIC_PID 2>/dev/null
        sleep 2
        sudo kill -9 $TRAFFIC_PID 2>/dev/null
    fi
    
    # Kill xApp only if not cleared (یعنی manual stop نخواسته)
    if [ ! -z "$XAPP_PID" ] && kill -0 $XAPP_PID 2>/dev/null; then
        print_msg "$YELLOW" "Stopping xApp (PID: $XAPP_PID)..."
        sudo kill -TERM $XAPP_PID 2>/dev/null
        sleep 2
        sudo kill -9 $XAPP_PID 2>/dev/null
    fi
    
    # Cleanup any remaining iperf processes
    sudo pkill -f "iperf3" 2>/dev/null
    
    print_msg "$GREEN" "Cleanup completed!"
    print_msg "$BLUE" "\nExperiment log saved to: $EXPERIMENT_LOG"
    exit 0
}

# Trap CTRL+C and other signals
trap cleanup SIGINT SIGTERM EXIT

# Main execution
main() {
    print_msg "$BLUE" "=========================================="
    print_msg "$BLUE" "   Automated Experiment Runner (Long-Run)"
    print_msg "$BLUE" "=========================================="
    echo ""
    
    # Check if running as root
    if [ "$EUID" -ne 0 ]; then
        print_msg "$RED" "ERROR: This script must be run with sudo"
        print_msg "$YELLOW" "Usage: sudo $0"
        exit 1
    fi
    
    # Expand tilde in path
    XAPP_BINARY_EXPANDED="${XAPP_BINARY/#\~/$HOME}"
    
    # Check if files exist
    print_msg "$YELLOW" "Checking prerequisites..."
    check_file "$TRAFFIC_SCRIPT"
    check_file "$XAPP_BINARY_EXPANDED"
    
    # Make sure traffic script is executable
    chmod +x "$TRAFFIC_SCRIPT" 2>/dev/null
    
    print_msg "$GREEN" "✓ All files found!"
    echo ""
    
    # Step 1: Start traffic generation
    print_msg "$BLUE" "=========================================="
    print_msg "$BLUE" "STEP 1: Starting Traffic Generation"
    print_msg "$BLUE" "=========================================="
    print_msg "$YELLOW" "Command: $TRAFFIC_SCRIPT"
    echo ""
    
    # Run traffic generation in background
    $TRAFFIC_SCRIPT &
    TRAFFIC_PID=$!
    
    print_msg "$GREEN" "✓ Traffic generation started (PID: $TRAFFIC_PID)"
    echo ""
    
    # Wait for the specified delay
    print_msg "$YELLOW" "Waiting ${DELAY_SECONDS} seconds before starting xApp..."
    for i in $(seq $DELAY_SECONDS -1 1); do
        echo -ne "\r${YELLOW}  Starting xApp in $i seconds...${NC}"
        sleep 1
    done
    echo -e "\r${GREEN}  ✓ Ready to start xApp!                    ${NC}"
    echo ""
    
    # Step 2: Start xApp
    print_msg "$BLUE" "=========================================="
    print_msg "$BLUE" "STEP 2: Starting xApp Monitoring"
    print_msg "$BLUE" "=========================================="
    print_msg "$YELLOW" "Command: $XAPP_BINARY_EXPANDED"
    echo ""
    
    # Run xApp in background
    $XAPP_BINARY_EXPANDED &
    XAPP_PID=$!
    
    print_msg "$GREEN" "✓ xApp started (PID: $XAPP_PID)"
    echo ""
    
    # Monitor both processes simultaneously (به جای wait فقط traffic)
    print_msg "$BLUE" "=========================================="
    print_msg "$BLUE" "Experiment Running (Long Mode)"
    print_msg "$BLUE" "=========================================="
    print_msg "$GREEN" "Traffic Generation PID: $TRAFFIC_PID"
    print_msg "$GREEN" "xApp PID: $XAPP_PID"
    print_msg "$YELLOW" "Waiting for traffic to finish, then xApp runs extra ${XAPP_EXTRA_DURATION}s..."
    print_msg "$YELLOW" "\nPress CTRL+C to stop early"
    echo ""
    
    # Wait for traffic generation to complete
    wait $TRAFFIC_PID
    TRAFFIC_EXIT_CODE=$?
    
    print_msg "$GREEN" "\n✓ Traffic generation completed (Exit code: $TRAFFIC_EXIT_CODE)"
    
    # Now let xApp run extra duration (بدون stop خودکار)
    print_msg "$YELLOW" "Letting xApp run for additional ${XAPP_EXTRA_DURATION} seconds..."
    sleep $XAPP_EXTRA_DURATION
    
    # Optional: Ask if keep running longer
    echo ""
    print_msg "$YELLOW" "xApp extra duration finished. Do you want to stop now? (y/n) [10s timeout]"
    read -t 10 -n 1 response
    echo ""
    
    if [[ "$response" =~ ^[Nn]$ ]]; then
        print_msg "$GREEN" "xApp will continue indefinitely. PID: $XAPP_PID"
        print_msg "$YELLOW" "To stop later: sudo kill $XAPP_PID or Ctrl+C here"
        # Infinite wait (manual stop)
        wait $XAPP_PID
    else
        print_msg "$YELLOW" "Stopping xApp now..."
        # PID clear نمی‌شه، cleanup kill می‌کنه
    fi
    
    echo ""
    print_msg "$GREEN" "=========================================="
    print_msg "$GREEN" "Experiment Completed Successfully!"
    print_msg "$GREEN" "=========================================="
}

# Run main function
main

# This will trigger cleanup on exit
exit 0