#!/bin/bash

# DRL Data Generation Script with Bursty Traffic for URLLC (Fixed Version)
# 200s total: 50s fixed 8Mbps + 20s burst 16Mbps (repeating cycles)

# Configuration parameters
UE1_IP="10.201.1.100"  # UE1: mMTC slice (fixed low traffic)
UE2_IP="10.202.1.100"  # UE2: URLLC slice (base + bursts)
SERVER_IP="192.168.70.129"  # Core network IP
DATA_DIR="./drl_training_data_bursty"
LOG_DIR="./logs_bursty"
TOTAL_DURATION=200  # 200 seconds total run
CYCLE_FIXED=50      # 50 seconds fixed bitrate for URLLC
BURST_DURATION=20   # 20 seconds burst duration
URLLC_BASE_RATE="8M"  # Fixed 8Mbps for URLLC base
URLLC_BURST_RATE="16M" # Burst rate 16Mbps (2x base rate)
MMTC_RATE="2M"      # Fixed 2Mbps for mMTC
SAMPLING_INTERVAL=0.1  # 100ms intervals for stats

# Create directories
mkdir -p $DATA_DIR
mkdir -p $LOG_DIR

# Function to start iperf server (one per slice)
start_iperf_server() {
    echo "Starting iperf3 servers..."
    iperf3 -s -p 5201 > $LOG_DIR/server_mmtc.log 2>&1 &  # For mMTC
    SERVER1_PID=$!
    iperf3 -s -p 5202 > $LOG_DIR/server_urllc.log 2>&1 &  # For URLLC
    SERVER2_PID=$!
    sleep 2
}

# Function to stop iperf server
stop_iperf_server() {
    echo "Stopping iperf3 servers..."
    kill $SERVER1_PID 2>/dev/null
    kill $SERVER2_PID 2>/dev/null
}

# Function to collect network statistics (expanded for more metrics)
collect_stats() {
    local experiment_id=$1
    local timestamp=$(date +%s)
    
    echo "Collecting stats for Experiment $experiment_id"
    
    local output_file="$DATA_DIR/experiment_${experiment_id}.csv"
    
    # CSV header (expanded: add burst flag)
    echo "timestamp,mmtc_traffic_rate,urllc_traffic_rate,mmtc_throughput,urllc_throughput,mmtc_latency,urllc_latency,mmtc_jitter,urllc_jitter,mmtc_packet_loss,urllc_packet_loss,is_burst" > $output_file
    
    local start_time=$(date +%s)
    while [ $(($(date +%s) - start_time)) -lt $TOTAL_DURATION ]; do
        local current_time=$(date +%s.%3N)
        
        # Latency with ping (to server)
        local mmtc_latency=$(ip netns exec ue1 ping -c 1 -W 1 $SERVER_IP 2>/dev/null | grep 'time=' | cut -d'=' -f4 | cut -d' ' -f1 || echo "0")
        local urllc_latency=$(ip netns exec ue2 ping -c 1 -W 1 $SERVER_IP 2>/dev/null | grep 'time=' | cut -d'=' -f4 | cut -d' ' -f1 || echo "0")
        
        # Throughput, jitter, packet loss: parse from iperf logs
        local mmtc_metrics=$(parse_iperf_results $LOG_DIR/mmtc_exp${experiment_id}.log)
        local urllc_metrics=$(parse_iperf_results $LOG_DIR/urllc_exp${experiment_id}.log)
        
        local mmtc_throughput=$(echo $mmtc_metrics | cut -d',' -f1)
        local mmtc_jitter=$(echo $mmtc_metrics | cut -d',' -f2)
        local mmtc_packet_loss=$(echo $mmtc_metrics | cut -d',' -f3)
        
        local urllc_throughput=$(echo $urllc_metrics | cut -d',' -f1)
        local urllc_jitter=$(echo $urllc_metrics | cut -d',' -f2)
        local urllc_packet_loss=$(echo $urllc_metrics | cut -d',' -f3)
        
        # Detect if in burst (based on time: every 70s cycle = 50s fixed + 20s burst)
        local elapsed=$(($(date +%s) - start_time))
        local cycle_pos=$((elapsed % 70))  # 70s cycle (50 fixed + 20 burst)
        local is_burst=0
        local urllc_rate="8"  # Base rate 8 Mbps (corrected!)
        if [ $cycle_pos -ge 50 ] && [ $cycle_pos -lt 70 ]; then  # 50-69s in cycle = burst
            is_burst=1
            urllc_rate="16"  # Burst rate 16 Mbps (corrected!)
        fi
        
        echo "$current_time,2,${urllc_rate},${mmtc_throughput},${urllc_throughput},${mmtc_latency},${urllc_latency},${mmtc_jitter},${urllc_jitter},${mmtc_packet_loss},${urllc_packet_loss},${is_burst}" >> $output_file
        
        sleep $SAMPLING_INTERVAL
    done
}

# Function to parse iperf results (expanded)
parse_iperf_results() {
    local log_file=$1
    if [ ! -f "$log_file" ]; then
        echo "0,0,0"
        return
    fi
    local throughput=$(grep "receiver" $log_file | awk '{print $7}' | tail -1 || echo "0")
    local jitter=$(grep "ms" $log_file | awk '{print $9}' | tail -1 || echo "0")
    local packet_loss=$(grep "receiver" $log_file | awk '{print $12}' | tail -1 || echo "0")
    
    echo "$throughput,$jitter,$packet_loss"
}

# Function to run experiment with periodic bursty traffic
run_experiment() {
    local experiment_id=$1
    
    echo "=== Experiment $experiment_id: mMTC fixed 2Mbps, URLLC 8Mbps base + 16Mbps bursts (20s every 70s) ==="
    
    # Start mMTC traffic (fixed, continuous)
    ip netns exec ue1 iperf3 -c $SERVER_IP -p 5201 -t $TOTAL_DURATION -b $MMTC_RATE -u > $LOG_DIR/mmtc_exp${experiment_id}.log 2>&1 &
    MMTC_PID=$!
    
    # Start URLLC traffic with periodic bursts (continuous stream, no gaps)
    (
        start_time=$(date +%s)
        
        # Run continuous iperf with dynamic rate adjustment
        while [ $(($(date +%s) - start_time)) -lt $TOTAL_DURATION ]; do
            elapsed=$(($(date +%s) - start_time))
            remaining=$((TOTAL_DURATION - elapsed))
            
            if [ $remaining -le 0 ]; then
                break
            fi
            
            cycle_pos=$((elapsed % 70))  # 70s cycle
            
            # Determine duration and rate for current phase
            if [ $cycle_pos -ge 50 ]; then
                # In burst phase (50-69s in cycle)
                rate=$URLLC_BURST_RATE  # 16M
                phase_remaining=$((70 - cycle_pos))  # Time left in burst
                if [ $phase_remaining -gt $remaining ]; then
                    duration=$remaining
                else
                    duration=$phase_remaining
                fi
            else
                # In normal phase (0-49s in cycle)
                rate=$URLLC_BASE_RATE  # 8M
                phase_remaining=$((50 - cycle_pos))  # Time left in normal phase
                if [ $phase_remaining -gt $remaining ]; then
                    duration=$remaining
                else
                    duration=$phase_remaining
                fi
            fi
            
            # Run iperf for the calculated duration (continuous, no gaps)
            ip netns exec ue2 iperf3 -c $SERVER_IP -p 5202 -t $duration -b $rate -u 2>&1
        done
    ) > $LOG_DIR/urllc_exp${experiment_id}.log 2>&1 &
    URLLC_PID=$!
    
    # Collect statistics
    collect_stats $experiment_id &
    STATS_PID=$!
    
    wait $MMTC_PID
    wait $URLLC_PID
    wait $STATS_PID
    
    echo "Experiment $experiment_id completed"
    sleep 5
}

# Main execution
main() {
    echo "========================================"
    echo "Starting Periodic Bursty Traffic Generation"
    echo "Total Duration: 200s"
    echo "Cycle: 50s base (8Mbps) + 20s burst (16Mbps) = 70s"
    echo "mMTC: Fixed 2Mbps"
    echo "========================================"
    
    start_iperf_server
    
    experiment_id=1
    run_experiment $experiment_id  # Single experiment
    
    stop_iperf_server
    
    echo "========================================"
    echo "Data generation completed!"
    echo "Generated data files are in: $DATA_DIR"
    echo "Log files are in: $LOG_DIR"
    echo "========================================"
}

trap stop_iperf_server SIGINT SIGTERM

main

echo "Periodic bursty traffic generation completed successfully!"