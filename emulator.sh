#!/usr/bin/env bash

# ==============================================================================
# ESP32 / ESP32-S3 QEMU INTERACTIVE EMULATION TEST BENCH
# ==============================================================================
# Ensure script is being run within an initialized shell environment or has IDF_PATH
if [ -z "$IDF_PATH" ]; then
    echo "❌ Error: ESP-IDF environment is not sourced."
    echo "Please run: 'source /path/to/esp-idf/export.sh' first before launching this script."
    exit 1
fi

set -euo pipefail

# Configurations
LOG_DIR="./firmware_logs"
mkdir -p "$LOG_DIR"

# 1. ENVIRONMENT VALIDATION & AUTOMATED TOOL CHECK
check_and_install_tools() {
    echo "🔍 Checking ESP-IDF tools framework and QEMU binary availability..."
    
    # Run the idf_tools manager to verify/install the QEMU Xtensa toolchain packages
    if ! python "$IDF_PATH/tools/idf_tools.py" check | grep -q "qemu-xtensa"; then
        echo "⚠️  Xtensa QEMU emulator package is missing or unlinked for ESP-IDF."
        echo "⚙️  Installing 'qemu-xtensa' via Espressif tool manager..."
        python "$IDF_PATH/tools/idf_tools.py" install qemu-xtensa
        
        echo "🔄 Re-exporting tool paths to current shell instance..."
        . "$IDF_PATH/export.sh"
    else
        echo "✅ QEMU Xtensa toolchain package verified."
    fi
}

# 2. MENU DRIVEN WORKFLOW
show_menu() {
    clear
    echo "================================================================="
    echo "       🚀 ESP-IDF XTENSA QEMU FIRMWARE EMULATION ENGINE          "
    echo "================================================================="
    echo " 1) Run Emulation: ESP32-S3 target"
    echo " 2) Run Emulation: ESP32-WROOM target (Legacy ESP32)"
    echo " 3) Re-check / Repair QEMU Package Toolchain"
    echo " 4) Exit"
    echo "================================================================="
    printf "Please select an option [1-4]: "
}

# 3. FIRMWARE SELECTION INTERFACE
select_firmware_dir() {
    local target_chip=$1
    echo -e "\n📂 Scanning directory for valid ESP-IDF firmware projects..."
    
    # Find all unique project directories containing CMakeLists.txt and main files
    IFS=$'\n' read -r -d '' -a subdirs < <(find . -maxdepth 3 -type f -name "CMakeLists.txt" -not -path '*/.*' -printf '%h\n' | sort -u && printf '\0') || true

    if [ ${#subdirs[@]} -eq 0 ]; then
        echo "❌ No project directories with a CMakeLists.txt found nearby."
        echo "Please place this runner script inside or right above your firmware source folder."
        read -n 1 -s -r -p "Press any key to return..."
        return 1
    fi

    echo "Select which firmware application to compile and run:"
    for i in "${!subdirs[@]}"; do
        echo " [$((i+1))] ${subdirs[$i]}"
    done
    
    printf "Choose a firmware option [1-${#subdirs[@]}]: "
    read -r fw_choice
    
    if ! [[ "$fw_choice" =~ ^[0-9]+$ ]] || [ "$fw_choice" -lt 1 ] || [ "$fw_choice" -gt "${#subdirs[@]}" ]; then
        echo "❌ Invalid project selection."
        sleep 2
        return 1
    fi

    local selected_dir="${subdirs[$((fw_choice-1))]}"
    execute_qemu_test "$selected_dir" "$target_chip"
}

# 4. TARGET COMPILATION AND EMULATOR INVOCATION
execute_qemu_test() {
    local fw_path=$1
    local target_chip=$2
    local timestamp
    timestamp=$(date +"%Y%m%d_%H%M%S")
    local log_file="$LOG_DIR/${target_chip}_${timestamp}_test_results.log"

    echo -e "\n⚡ Target Selected: $target_chip"
    echo "📂 Project Directory: $fw_path"
    echo "📝 Execution Log File: $log_file"
    echo "-----------------------------------------------------------------"

    # Move inside target project workspace
    pushd "$fw_path" > /dev/null

    echo "🛠️  Setting ESP-IDF target chip framework to $target_chip..."
    idf.py set-target "$target_chip" 2>&1 | tee -a "../$log_file"

    echo "🏗️  Compiling application binaries & system bootloaders..."
    idf.py build 2>&1 | tee -a "../$log_file"

    echo "🎮 Launching Xtensa Emulator ($target_chip) with automated Serial Log Redirect..."
    echo "💡 [TIP] To exit the active QEMU terminal monitor, press: 'Ctrl + ]'"
    sleep 2

    # Execute firmware securely within Espressif's custom-built QEMU engine
    # 'qemu monitor' maps serial feedback directly to standard stdout channels
    if ! idf.py qemu monitor 2>&1 | tee -a "../$log_file"; then
        echo "⚠️  QEMU instance stopped or completed evaluation." | tee -a "../$log_file"
    fi

    popd > /dev/null
    echo -e "\n✅ Emulation finished. Comprehensive text logs saved to: $log_file"
    read -n 1 -s -r -p "Press any key to return to main dashboard..."
}

# --- Runtime Initialization Entrypoint ---
check_and_install_tools

while true; do
    show_menu
    read -r choice
    case $choice in
        1)
            select_firmware_dir "esp32s3"
            ;;
        2)
            # ESP32-WROOM uses the baseline 'esp32' Xtensa architecture translation layer
            select_firmware_dir "esp32"
            ;;
        3)
            check_and_install_tools
            read -n 1 -s -r -p "Tool configuration verified. Press any key..."
            ;;
        4)
            echo "👋 Safely shutting down test bench environment. Goodbye!"
            exit 0
            ;;
        *)
            echo "❌ Invalid response parameter."
            sleep 1
            ;;
    esac
done
