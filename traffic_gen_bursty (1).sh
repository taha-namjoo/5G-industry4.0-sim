#!/bin/bash

# DRL Data Generation Script with Bursty Traffic for URLLC
# Based on your script, modified for mMTC (fixed) and URLLC (bursty with variable burst rates)

# Configuration parameters
UE1_IP="10.201.1.100"  # UE1: mMTC slice (fixed low traffic)
UE2_IP="10.202.1.100"  # UE2: URLLC slice (base + bursts)
SERVER_IP="192.168.70.129"  # Core network IP
DATA_DIR="./drl_training_data_bursty"
LOG_DIR="./logs_bursty"
TOTAL_DURATION=300  # 5 minutes total run
BURST_INTERVAL=30   # Burst every 30 seconds
BURST_DURATION=5    # Burst lasts 5 seconds
MMTC_RATE=5M        # Fixed 5Mbps for mMTC
URLLC_BASE_RATE=10M # Base 10Mbps for URLLC
URLLC_BURST_MIN=50  # Min burst rate in Mbps
URLLC_BURST_MAX=150 # Max burst rate in Mbps
SAMPLING_INTERVAL=0.1  # 100ms intervals

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
    
    echo "Collecting stats for Experiment: $experiment_id"
    
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
        
        # Detect if in burst (based on time)
        local elapsed=$(($(date +%s) - start_time))
        local is_burst=0
        local urllc_rate="10"
        if [ $((elapsed % BURST_INTERVAL)) -lt $BURST_DURATION ]; then
            is_burst=1
            urllc_rate="variable (50-150)"
        fi
        
        echo "$current_time,5,${urllc_rate},${mmtc_throughput},${urllc_throughput},${mmtc_latency},${urllc_latency},${mmtc_jitter},${urllc_jitter},${mmtc_packet_loss},${urllc_packet_loss},${is_burst}" >> $output_file
        
        sleep $SAMPLING_INTERVAL
    done
}

# Function to parse iperf results (expanded)
parse_iperf_results() {
    local log_file=$1
    local throughput=$(grep "receiver" $log_file | awk '{print $7}' | tail -1 || echo "0")
    local jitter=$(grep "ms" $log_file | awk '{print $9}' | tail -1 || echo "0")
    local packet_loss=$(grep "receiver" $log_file | awk '{print $12}' | tail -1 || echo "0")
    
    echo "$throughput,$jitter,$packet_loss"
}

# Function to run experiment with bursty traffic
run_experiment() {
    local experiment_id=$1
    
    echo "=== Experiment $experiment_id: mMTC fixed 5Mbps, URLLC base 10Mbps + variable bursts (50-150Mbps) ==="
    
    # Start mMTC traffic (fixed)
    ip netns exec ue1 iperf3 -c $SERVER_IP -p 5201 -t $TOTAL_DURATION -b $MMTC_RATE > $LOG_DIR/mmtc_exp${experiment_id}.log 2>&1 &
    MMTC_PID=$!
    
    # Start URLLC traffic with bursts (loop in background)
    (
        start_time=$(date +%s)
        while [ $(($(date +%s) - start_time)) -lt $TOTAL_DURATION ]; do
            elapsed=$(($(date +%s) - start_time))
            if [ $((elapsed % BURST_INTERVAL)) -lt $BURST_DURATION ]; then
                burst_rate=$((URLLC_BURST_MIN + RANDOM % (URLLC_BURST_MAX - URLLC_BURST_MIN + 1)))
                rate="${burst_rate}M"
            else
                rate=$URLLC_BASE_RATE
            fi
            ip netns exec ue2 iperf3 -c $SERVER_IP -p 5202 -t 1 -b $rate
            sleep 0.1  # No sleep, continuous
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
    echo "Starting Bursty Traffic Generation..."
    
    start_iperf_server
    
    experiment_id=1
    run_experiment $experiment_id  # Single experiment for simplicity; loop if needed
    
    stop_iperf_server
    
    echo "Data generation completed!"
    echo "Generated data files are in: $DATA_DIR"
    echo "Log files are in: $LOG_DIR"
}

trap stop_iperf_server SIGINT SIGTERM

main

echo "Bursty traffic generation completed successfully!"
