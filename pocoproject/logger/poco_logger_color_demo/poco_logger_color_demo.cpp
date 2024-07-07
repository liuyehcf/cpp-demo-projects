#include <Poco/AutoPtr.h>
#include <Poco/ConsoleChannel.h>
#include <Poco/FormattingChannel.h>
#include <Poco/Logger.h>
#include <Poco/PatternFormatter.h>

namespace ck_color {
using UInt8 = uint8_t;
using UInt64 = uint64_t;
std::string setColor(UInt64 hash) {
    /// Make a random RGB color that has constant brightness.
    /// https://en.wikipedia.org/wiki/YCbCr

    /// Note that this is darker than the middle relative luminance, see "Gamma_correction" and "Luma_(video)".
    /// It still looks awesome.
    UInt8 y = 128;

    UInt8 cb = static_cast<UInt8>(hash % 256);
    UInt8 cr = static_cast<UInt8>(hash / 256 % 256);

    UInt8 r = static_cast<UInt8>(std::max(0.0, std::min(255.0, y + 1.402 * (cr - 128))));
    UInt8 g = static_cast<UInt8>(std::max(0.0, std::min(255.0, y - 0.344136 * (cb - 128) - 0.714136 * (cr - 128))));
    UInt8 b = static_cast<UInt8>(std::max(0.0, std::min(255.0, y + 1.772 * (cb - 128))));

    /// ANSI escape sequence to set 24-bit foreground font color in terminal.
    return "\033[38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
}

const char* setColorForLogPriority(int priority) {
    if (priority < 1 || priority > 8) return "";

    static const char* colors[] = {
            "",
            "\033[1;41m", /// Fatal
            "\033[7;31m", /// Critical
            "\033[1;31m", /// Error
            "\033[0;31m", /// Warning
            "\033[0;33m", /// Notice
            "\033[1m",    /// Information
            "",           /// Debug
            "\033[2m",    /// Trace
    };

    return colors[priority];
}

const char* resetColor() {
    return "\033[0m";
}
} // namespace ck_color

class ColorfulPatternFormatter : public Poco::PatternFormatter {
public:
    ColorfulPatternFormatter(Poco::AutoPtr<Poco::PatternFormatter> formatter) : _formatter(std::move(formatter)) {}

    void format(const Poco::Message& msg, std::string& text) override {
        // You can customize here. Here is an example
        if (msg.getPriority() >= Poco::Message::PRIO_INFORMATION) {
            text.append(ck_color::setColor(std::hash<std::string>{}(msg.getText())));
            _formatter->format(msg, text);
            text.append(ck_color::resetColor());
        } else {
            text.append(ck_color::setColorForLogPriority(msg.getPriority()));
            _formatter->format(msg, text);
            text.append(ck_color::resetColor());
        }
    }

private:
    Poco::AutoPtr<Poco::PatternFormatter> _formatter;
};

int main() {
    {
        Poco::AutoPtr<Poco::ConsoleChannel> console_channel(new Poco::ConsoleChannel);

        Poco::AutoPtr<Poco::PatternFormatter> formatter(
                new Poco::PatternFormatter("%Y-%m-%d %H:%M:%S [%P:%I] [%p] %s - %t"));
        Poco::AutoPtr<Poco::PatternFormatter> colorful_formatter(new ColorfulPatternFormatter(formatter));
        Poco::AutoPtr<Poco::FormattingChannel> formatting_channel(
                new Poco::FormattingChannel(colorful_formatter, console_channel));

        Poco::Logger& logger =
                Poco::Logger::create("MultiChannelLogger", formatting_channel, Poco::Message::PRIO_TRACE);
        logger.trace("This is a trace message(1).");
        logger.trace("This is a trace message(2).");
        logger.debug("This is an debug message(1).");
        logger.debug("This is an debug message(2).");
        logger.information("This is an information message(1).");
        logger.information("This is an information message(2).");
        logger.warning("This is a warning message(1).");
        logger.warning("This is a warning message(2).");
        logger.error("This is an error message(1).");
        logger.error("This is an error message(2).");
        logger.critical("This is an critical message(1).");
        logger.critical("This is an critical message(2).");
        logger.fatal("This is a fatal message(1).");
        logger.fatal("This is a fatal message(2).");
    }
    return 0;
}
