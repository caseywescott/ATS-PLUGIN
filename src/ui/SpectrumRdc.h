#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace ats::ui::rdcDisplay
{

struct RdcFit
{
    std::vector<double> coefficients;
    double minBark = 0.0;
    double maxBark = 1.0;
    bool valid = false;

    [[nodiscard]] double evaluate(double normalizedBark) const noexcept
    {
        if (!valid || coefficients.empty()) return 0.0;
        const auto x = std::clamp(normalizedBark, 0.0, 1.0);
        double value = coefficients[0];
        for (std::size_t i = 1; i < coefficients.size(); ++i)
            value += coefficients[i] * 2.0 * std::cos(3.14159265358979323846
                                                        * static_cast<double>(i) * x);
        return value;
    }
};

[[nodiscard]] inline double bark(double frequencyHz) noexcept
{
    // The shipped JUCE helper clamps every Bark input to 1 Hz, including the
    // evaluation grid endpoints.
    return 6.0 * std::asinh(std::max(1.0, frequencyHz) / 600.0);
}

[[nodiscard]] inline double barkNorm(double frequencyHz, double minHz, double maxHz) noexcept
{
    const auto lo = bark(std::max(1.0, minHz));
    const auto hi = std::max(lo + 1.0e-9, bark(std::max(1.0, maxHz)));
    return std::clamp((bark(std::max(1.0, frequencyHz)) - lo) / (hi - lo), 0.0, 1.0);
}

[[nodiscard]] inline std::vector<double> buildBasisRow(double normalizedBark, int order)
{
    const auto count = static_cast<std::size_t>(std::max(0, order)) + 1;
    std::vector<double> row(count, 0.0);
    row[0] = 1.0;
    const auto c1 = std::cos(3.14159265358979323846
                             * std::clamp(normalizedBark, 0.0, 1.0));
    double previous = 1.0;
    double current = c1;
    for (std::size_t i = 1; i < count; ++i)
    {
        row[i] = 2.0 * current;
        const auto next = 2.0 * c1 * current - previous;
        previous = current;
        current = next;
    }
    return row;
}

[[nodiscard]] inline std::optional<std::vector<double>> solveGaussian(
    std::vector<std::vector<double>> matrix, std::vector<double> rhs)
{
    const auto n = matrix.size();
    if (n == 0 || rhs.size() != n) return std::nullopt;
    for (const auto& row : matrix)
        if (row.size() != n) return std::nullopt;

    for (std::size_t column = 0; column < n; ++column)
    {
        auto pivot = column;
        for (std::size_t row = column + 1; row < n; ++row)
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) pivot = row;
        if (std::abs(matrix[pivot][column]) < 1.0e-9) return std::nullopt;
        if (pivot != column)
        {
            std::swap(matrix[pivot], matrix[column]);
            std::swap(rhs[pivot], rhs[column]);
        }
        for (std::size_t row = column + 1; row < n; ++row)
        {
            const auto factor = matrix[row][column] / matrix[column][column];
            for (std::size_t k = column; k < n; ++k)
                matrix[row][k] -= factor * matrix[column][k];
            rhs[row] -= factor * rhs[column];
        }
    }

    std::vector<double> result(n, 0.0);
    for (std::size_t row = n; row-- > 0;)
    {
        double remainder = rhs[row];
        for (std::size_t column = row + 1; column < n; ++column)
            remainder -= matrix[row][column] * result[column];
        result[row] = remainder / matrix[row][row];
    }
    return result;
}

[[nodiscard]] inline RdcFit fitRDC(const std::vector<double>& frequenciesHz,
                                   const std::vector<double>& amplitudeNorms,
                                   double minHz, double maxHz,
                                   int order = 10, double regularization = 8.0e-3)
{
    RdcFit result;
    result.minBark = bark(std::max(1.0, minHz));
    result.maxBark = std::max(result.minBark + 1.0e-9, bark(std::max(1.0, maxHz)));
    const auto count = std::min(frequenciesHz.size(), amplitudeNorms.size());
    const auto fitOrder = std::clamp(order, 0, 15);
    const auto dimension = static_cast<std::size_t>(fitOrder + 1);
    std::vector<std::vector<double>> normal(dimension, std::vector<double>(dimension, 0.0));
    std::vector<double> rhs(dimension, 0.0);
    std::size_t active = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!std::isfinite(frequenciesHz[i]) || !std::isfinite(amplitudeNorms[i])
            || amplitudeNorms[i] <= 0.0)
            continue;
        const auto x = barkNorm(frequenciesHz[i], minHz, maxHz);
        const auto row = buildBasisRow(x, fitOrder);
        const auto amplitude = std::clamp(amplitudeNorms[i], 0.0, 1.0);
        const auto y = -120.0 + amplitude * 120.0;
        const auto weight = std::max(1.0e-4, amplitude);
        for (std::size_t r = 0; r < dimension; ++r)
        {
            rhs[r] += weight * row[r] * y;
            for (std::size_t c = 0; c < dimension; ++c)
                normal[r][c] += weight * row[r] * row[c];
        }
        ++active;
    }
    const auto effectiveOrder = std::min(10, std::max(1, static_cast<int>(active) - 1));
    if (fitOrder != effectiveOrder)
        return fitRDC(frequenciesHz, amplitudeNorms, minHz, maxHz,
                      effectiveOrder, regularization);
    for (std::size_t i = 1; i < dimension; ++i)
        normal[i][i] += std::max(0.0, regularization)
            * static_cast<double>(i * i * i * i);
    const auto solved = solveGaussian(std::move(normal), std::move(rhs));
    if (!solved) return result;
    result.coefficients = *solved;
    result.valid = true;
    return result;
}

[[nodiscard]] inline int boxRadiiForGauss(double sigma) noexcept
{ return std::max(0, static_cast<int>(std::ceil(std::max(0.0, sigma) * 1.5))); }

inline void boxBlurInPlace(std::vector<float>& values, int radius)
{
    radius = std::max(0, radius);
    if (values.size() < 2 || radius == 0) return;
    const auto source = values;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        const auto first = i > static_cast<std::size_t>(radius) ? i - static_cast<std::size_t>(radius) : 0;
        const auto last = std::min(values.size() - 1, i + static_cast<std::size_t>(radius));
        float sum = 0.0f;
        for (auto j = first; j <= last; ++j) sum += source[j];
        values[i] = sum / static_cast<float>(last - first + 1);
    }
}

[[nodiscard]] inline std::vector<float> makeSliceTimes(std::size_t frameCount,
                                                        std::size_t sliceCount)
{
    std::vector<float> result;
    if (frameCount == 0 || sliceCount == 0) return result;
    result.resize(sliceCount);
    for (std::size_t i = 0; i < sliceCount; ++i)
        result[i] = sliceCount == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(sliceCount - 1);
    return result;
}

} // namespace ats::ui::rdcDisplay
