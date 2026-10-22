#include <cpp-pinyin2/core/Types.h>

namespace cpp_pinyin::v2
{
    Result::Result(std::vector<Segment> syllables) : m_syllables(std::move(syllables)) {}

    bool Result::empty() const noexcept { return m_syllables.empty(); }

    size_t Result::size() const noexcept { return m_syllables.size(); }

    const Segment &Result::operator[](size_t i) const noexcept { return m_syllables[i]; }

    const Segment &Result::at(size_t i) const { return m_syllables.at(i); }

    Segment &Result::operator[](size_t i) noexcept { return m_syllables[i]; }

    Segment &Result::at(size_t i) { return m_syllables.at(i); }

    Result::const_iterator Result::begin() const noexcept { return m_syllables.begin(); }

    Result::const_iterator Result::end() const noexcept { return m_syllables.end(); }

    Result::const_iterator Result::cbegin() const noexcept { return m_syllables.cbegin(); }

    Result::const_iterator Result::cend() const noexcept { return m_syllables.cend(); }

    std::string Result::join(std::string_view delimiter) const {
        size_t total = 0;
        for (const auto &s : m_syllables) {
            total += s.rendered.size() + delimiter.size();
        }

        std::string result;
        result.reserve(total);
        bool first = true;

        for (const auto &s : m_syllables) {
            if (!first) {
                result += delimiter;
            }
            result += s.rendered;
            first = false;
        }

        return result;
    }

    std::vector<std::string> Result::toVector() const {
        std::vector<std::string> result;
        result.reserve(m_syllables.size());
        for (const auto &s : m_syllables) {
            result.push_back(s.rendered);
        }
        return result;
    }
}  // namespace cpp_pinyin::v2
