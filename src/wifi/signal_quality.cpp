#include "signal_quality.h"

#include <chrono>
#include <random>

namespace {

std::string generate_random_string(size_t length) {
    const std::string characters = "abcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, characters.size() - 1);

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += characters[distrib(gen)];
    }
    return result;
}

} // namespace

// Remove RSSI samples older than 1 second
void SignalQualityCalculator::cleanup_old_rssi_data() {
    const auto now = std::chrono::steady_clock::now();
    const auto cutoff = now - averaging_window_;

    // Erase-remove idiom for data older than cutoff
    std::erase_if(rssi_data_, [&](const RssiEntry &entry) { return entry.timestamp < cutoff; });
}

void SignalQualityCalculator::cleanup_old_snr_data() {
    const auto now = std::chrono::steady_clock::now();
    const auto cutoff = now - averaging_window_;

    // Erase-remove idiom for data older than cutoff
    std::erase_if(snr_data_, [&](const SnrEntry &entry) { return entry.timestamp < cutoff; });
}

void SignalQualityCalculator::cleanup_old_fec_data() {
    const auto now = std::chrono::steady_clock::now();
    const auto cutoff = now - averaging_window_;

    std::erase_if(fec_data_, [&](const FecEntry &entry) { return entry.timestamp < cutoff; });
}

void SignalQualityCalculator::add_rssi(uint8_t ant1, uint8_t ant2) {
    std::lock_guard lock(mutex_);

    RssiEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.ant1 = ant1;
    entry.ant2 = ant2;
    rssi_data_.push_back(entry);
}

void SignalQualityCalculator::add_snr(int8_t ant1, int8_t ant2) {
    std::lock_guard lock(mutex_);

    SnrEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.ant1 = ant1;
    entry.ant2 = ant2;
    snr_data_.push_back(entry);
}

SignalQualityCalculator::SignalQuality SignalQualityCalculator::calculate_signal_quality() {
    SignalQuality ret;
    std::lock_guard lock(mutex_);

    // Make sure we clean up old data first
    cleanup_old_rssi_data();
    cleanup_old_snr_data();
    cleanup_old_fec_data();

    auto avg_rssi = get_average(rssi_data_);
    auto avg_snr = get_average(snr_data_);

    // Map the RSSI from range 0..126 to 0..100
    float rssi0 = map_range(avg_rssi.first, 0.f, 126.f, 0.f, 100.f);
    float rssi1 = map_range(avg_rssi.second, 0.f, 126.f, 0.f, 100.f);

    // Map the SNR from range 0..60 to 0..100
    float snr0 = map_range(avg_snr.first, 0.f, 60.f, 0.f, 100.f);
    float snr1 = map_range(avg_snr.second, 0.f, 60.f, 0.f, 100.f);

    auto [p_recovered, p_lost, p_total] = get_accumulated_fec_data();

    ret.lost_last_second = p_lost;
    ret.recovered_last_second = p_recovered;
    ret.total_last_second = p_total;

    ret.rssi[0] = avg_rssi.first;
    ret.rssi[1] = avg_rssi.second;
    ret.snr[0] = avg_snr.first;
    ret.snr[1] = avg_snr.second;
    ret.idr_code = idr_code_;

    // Link Quality = (w1 * RSSI) + (w2 * SNR)
    ret.link_score[0] = 0.3f * rssi0 + 0.7f * snr0;
    ret.link_score[1] = 0.3f * rssi1 + 0.7f * snr1;

    return ret;
}

std::tuple<uint32_t, uint32_t, uint32_t> SignalQualityCalculator::get_accumulated_fec_data() const {
    uint32_t p_recovered = 0;
    uint32_t p_all = 0;
    uint32_t p_lost = 0;

    for (const auto &data : fec_data_) {
        p_all += data.all;
        p_recovered += data.recovered;
        p_lost += data.lost;
    }

    return {p_recovered, p_lost, p_all};
}

void SignalQualityCalculator::add_fec(uint32_t p_all, uint32_t p_recovered, uint32_t p_lost) {
    std::lock_guard lock(mutex_);

    FecEntry entry;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.all = p_all;
    entry.recovered = p_recovered;
    entry.lost = p_lost;

    if (p_lost > 0) {
        idr_code_ = generate_random_string(4);
    }

    fec_data_.push_back(entry);
}
