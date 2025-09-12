#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

inline double map_range(double value, double inputMin, double inputMax, double outputMin, double outputMax) {
    return outputMin + ((value - inputMin) * (outputMax - outputMin) / (inputMax - inputMin));
}

class SignalQualityCalculator {
public:
    struct SignalQuality {
        int lost_last_second;
        int recovered_last_second;
        int total_last_second;
        int quality;
        float snr; // Signal to noice ratio
        std::string idr_code;
    };

    SignalQualityCalculator() = default;
    ~SignalQualityCalculator() = default;

    /// Add a new RSSI entry with current timestamp
    void add_rssi(uint8_t ant1, uint8_t ant2);

    /// Add a new SNR entry with current timestamp
    void add_snr(int8_t ant1, int8_t ant2);

    /// Add new FEC entry with current timestamp
    void add_fec(uint32_t p_all, uint32_t p_recovered, uint32_t p_lost);

    template <class T>
    float get_average(const T &array) {
        std::lock_guard lock(mutex_);

        float sum1 = 0.f;
        float sum2 = 0.f;
        int count = static_cast<int>(array.size());

        if (count > 0) {
            for (auto &entry : array) {
                sum1 += entry.ant1;
                sum2 += entry.ant2;
            }
            sum1 /= count;
            sum2 /= count;
        }

        // We'll take the maximum of the two average RSSI values
        const float avg = std::max(sum1, sum2);
        return avg;
    }

    /// Calculate signal quality over the averaging window
    SignalQuality calculate_signal_quality();

    static SignalQualityCalculator &get_instance() {
        static SignalQualityCalculator instance;
        return instance;
    }

private:
    /// Sum up FEC data over the averaging window
    std::tuple<uint32_t, uint32_t, uint32_t> get_accumulated_fec_data() const;

    // Helper methods to remove old entries
    void cleanup_old_rssi_data();
    void cleanup_old_snr_data();
    void cleanup_old_fec_data();

    // We store a timestamp for each RSSI entry
    struct RssiEntry {
        std::chrono::steady_clock::time_point timestamp;
        uint8_t ant1;
        uint8_t ant2;
    };

    // We store a timestamp for each RSSI entry
    struct SnrEntry {
        std::chrono::steady_clock::time_point timestamp;
        int8_t ant1;
        int8_t ant2;
    };

    // We store a timestamp for each FEC entry
    struct FecEntry {
        std::chrono::steady_clock::time_point timestamp;
        uint32_t all;
        uint32_t recovered;
        uint32_t lost;
    };

private:
    const std::chrono::seconds averaging_window_{std::chrono::seconds(1)};

    mutable std::recursive_mutex mutex_;

    std::vector<RssiEntry> rssi_data_;

    std::vector<SnrEntry> snr_data_;

    std::vector<FecEntry> fec_data_;

    // 4-character random string
    std::string idr_code_{"aaaa"};
};
