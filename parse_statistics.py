#!/usr/bin/env python3
import os
import re
import sys
import argparse
import numpy as np
import matplotlib.pyplot as plt

def parse_log_file(file_path):
    """Extracts cycle counts from raw logs containing ---RAW_SAMPLE::ITER=X,CYCLES=Y---"""
    cycles = []
    # Pattern matches text structured exactly like your C firmware output stream
    pattern = re.compile(r'---RAW_SAMPLE::ITER=\d+,CYCLES=(\d+)---')
    
    if not os.path.exists(file_path):
        print(f"❌ Error: Target log file '{file_path}' does not exist.")
        sys.exit(1)
        
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            match = pattern.search(line)
            if match:
                cycles.append(int(match.group(1)))
                
    return np.array(cycles)

def compute_statistics(data, cpu_freq_mhz=240):
    """Calculates comprehensive statistical dispersion metrics"""
    if len(data) == 0:
        return None
        
    # Convert cycles to microseconds (µs) for conventional academic reporting
    # Cycles / (MHz) = Microseconds
    data_us = data / cpu_freq_mhz
    
    metrics = {
        "count": len(data),
        "min_cyc": np.min(data),
        "max_cyc": np.max(data),
        "mean_cyc": np.mean(data),
        "median_cyc": np.median(data),
        "std_dev_cyc": np.std(data),
        "variance_cyc": np.var(data),
        "p95_cyc": np.percentile(data, 95),
        "p99_cyc": np.percentile(data, 99),
        
        "min_us": np.min(data_us),
        "max_us": np.max(data_us),
        "mean_us": np.mean(data_us),
        "median_us": np.median(data_us),
        "std_dev_us": np.std(data_us),
        "p95_us": np.percentile(data_us, 95),
        "p99_us": np.percentile(data_us, 99)
    }
    return metrics

def generate_plots(data, metrics, output_image_path, cpu_freq_mhz=240):
    """Generates a clean distribution histogram with statistical annotations"""
    data_us = data / cpu_freq_mhz
    
    # Establish styled dark/light theme properties for presentation elegance
    plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')
    fig, ax = plt.subplots(figsize=(9, 5.5), dpi=150)
    
    # Render Histogram
    n, bins, patches = ax.hist(data_us, bins=40, color='#38bdf8', edgecolor='#0369a1', alpha=0.8, label='Observed Latency Samples')
    
    # Add Visual Markers for Crucial Dispersion Elements
    ax.axvline(metrics["mean_us"], color='#ef4444', linestyle='--', linewidth=2, label=f'Mean μ: {metrics["mean_us"]:.2f} µs')
    ax.axvline(metrics["median_us"], color='#10b981', linestyle='-', linewidth=2, label=f'Median m: {metrics["median_us"]:.2f} µs')
    ax.axvline(metrics["p95_us"], color='#f59e0b', linestyle=':', linewidth=2, label=f'95th Percentile: {metrics["p95_us"]:.2f} µs')
    
    # Layout and Text Annotations
    ax.set_title("ESP32-S3 Firmware Workload Latency Distribution (QEMU Emulation)", fontsize=13, fontweight='bold', pad=15)
    ax.set_xlabel("Execution Latency (Microseconds - µs)", fontsize=11, labelpad=8)
    ax.set_ylabel("Sample Bin Count Frequency", fontsize=11, labelpad=8)
    
    # Add metadata summary card inside the chart boundaries
    summary_text = (
        f"📊 Execution Context Summary:\n"
        f"-----------------------------------------\n"
        f"Total Samples (N): {metrics['count']}\n"
        f"Clock Frequency: {cpu_freq_mhz} MHz\n"
        f"Standard Dev (σ): {metrics['std_dev_us']:.3f} µs\n"
        f"Variance (σ²): {metrics['variance_cyc']:.1f} cycles²\n"
        f"99th Percentile: {metrics['p99_us']:.2f} µs\n"
        f"Min / Max Window: [{metrics['min_us']:.1f}, {metrics['max_us']:.1f}] µs"
    )
    
    # Box placement properties
    props = dict(boxstyle='round,pad=0.6', facecolor='#f8fafc', edgecolor='#cbd5e1', alpha=0.95)
    ax.text(0.97, 0.55, summary_text, transform=ax.transAxes, fontsize=9.5, fontfamily='monospace',
            verticalalignment='top', horizontalalignment='right', bbox=props)
    
    ax.legend(loc='upper right', frameon=True, facecolor='white', edgecolor='#e2e8f0')
    plt.tight_layout()
    
    plt.savefig(output_image_path, bbox_inches='tight')
    print(f"📈 Presentation graphic saved successfully to: {output_image_path}")
    plt.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Academic Post-Processing Log Parser for SuperNova Micro-Ledger.")
    parser.add_argument("log_file", help="Path to the target firmware output text log file")
    parser.add_argument("--freq", type=int, default=240, help="Emulated target processor speed in MHz (default: 240)")
    parser.add_argument("--out", default="latency_distribution.png", help="Filename for the generated slide plot output")
    
    args = parser.parse_args()
    
    print("⚡ Loading raw firmware log markers...")
    raw_data = parse_log_file(args.log_file)
    
    if len(raw_data) == 0:
        print("❌ Error: No matching '---RAW_SAMPLE::...---' format configurations detected inside the document.")
        sys.exit(1)
        
    stats = compute_statistics(raw_data, args.freq)
    
    # Output analytical tabular metrics directly to command line standard stream outputs
    print("\n" + "="*55)
    print("      📐 CALCULATED EMPIRICAL DISPERSION METRICS")
    print("="*55)
    print(f"  • Total Log Lines Ingested: {stats['count']}")
    print(f"  • Arithmetic Mean (μ)    : {stats['mean_cyc']:.1f} cycles  ({stats['mean_us']:.2f} µs)")
    print(f"  • Median (m)               : {stats['median_cyc']:.1f} cycles  ({stats['median_us']:.2f} µs)")
    print(f"  • Standard Deviation (σ)   : {stats['std_dev_cyc']:.1f} cycles  ({stats['std_dev_us']:.3f} µs)")
    print(f"  • Sample Variance (σ²)     : {stats['variance_cyc']:.1f} cycles²")
    print(f"  • 95th Percentile (p95)    : {stats['p95_cyc']:.1f} cycles  ({stats['p95_us']:.2f} µs)")
    print(f"  • 99th Percentile (p99)    : {stats['p99_cyc']:.1f} cycles  ({stats['p99_us']:.2f} µs)")
    print("="*55 + "\n")
    
    generate_plots(raw_data, stats, args.out, args.freq)
