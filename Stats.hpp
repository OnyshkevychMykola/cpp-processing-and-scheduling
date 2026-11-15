#pragma once

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <ostream>
#include <string>
#include <vector>

// Prints count / avg / p50 / p99 / max of a set of microsecond samples.
inline void printStats(std::ostream& out, const std::string& name, std::vector<double> values)
{
    out << std::left << std::setw(22) << name << std::right;
    if (values.empty())
    {
        out << " (no samples)\n";
        return;
    }
    std::sort(values.begin(), values.end());
    double total = 0.0;
    for (const double v : values)
    {
        total += v;
    }
    auto pct = [&](double p) {
        return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1))];
    };
    out << std::fixed << std::setprecision(1)
        << " n=" << std::setw(6) << values.size()
        << " avg=" << std::setw(8) << total / static_cast<double>(values.size())
        << " p50=" << std::setw(8) << pct(0.50)
        << " p99=" << std::setw(8) << pct(0.99)
        << " max=" << std::setw(8) << values.back() << " us\n";
}
