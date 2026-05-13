#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ParsedNumber {
    bool negative = false;
    std::uint64_t magnitude = 0;
};

enum class Endian {
    Little,
    Big,
};

struct Options {
    std::filesystem::path filePath;
    bool hasFilePath = false;
    std::optional<ParsedNumber> start;
    std::optional<ParsedNumber> end;
    std::optional<ParsedNumber> embed;
    int widthBits = 32;
    Endian endian = Endian::Little;
    bool showHelp = false;
};

struct ResolvedOptions {
    std::filesystem::path filePath;
    std::uint64_t fileSize = 0;
    std::uint64_t start = 0;
    std::uint64_t endExclusive = 0;
    std::optional<std::uint64_t> embedOffset;
    int widthBits = 32;
    Endian endian = Endian::Little;
};

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

std::string usage()
{
    return
        "Usage:\n"
        "  checksumTool.exe -f <file> [-s <start>] [-e <end>] [-u 8|16|32] [-m <offset>] [-n L|B]\n\n"
        "Addresses accept decimal or 0x-prefixed hexadecimal values.\n"
        "A negative -e value is resolved from the end of file as an exclusive end offset.\n"
        "A negative -m value is resolved from the end of file as the write offset.\n";
}

bool iequals(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const auto l = static_cast<unsigned char>(lhs[i]);
        const auto r = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(l) != std::tolower(r)) {
            return false;
        }
    }

    return true;
}

ParsedNumber parseNumber(std::string_view text, std::string_view optionName)
{
    if (text.empty()) {
        fail(std::string(optionName) + " requires a number");
    }

    ParsedNumber result;
    if (text.front() == '-' || text.front() == '+') {
        result.negative = text.front() == '-';
        text.remove_prefix(1);
    }

    if (text.empty()) {
        fail(std::string(optionName) + " requires digits after the sign");
    }

    int base = 10;
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }

    if (text.empty()) {
        fail(std::string(optionName) + " requires digits after the 0x prefix");
    }

    std::uint64_t value = 0;
    for (const char c : text) {
        int digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        }

        if (digit < 0 || digit >= base) {
            fail(std::string(optionName) + " contains an invalid numeric character");
        }

        const auto maxValue = std::numeric_limits<std::uint64_t>::max();
        if (value > (maxValue - static_cast<std::uint64_t>(digit)) / static_cast<std::uint64_t>(base)) {
            fail(std::string(optionName) + " is too large");
        }
        value = value * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(digit);
    }

    result.magnitude = value;
    return result;
}

std::string requireValue(int argc, char* argv[], int& index, std::string_view optionName)
{
    if (index + 1 >= argc) {
        fail(std::string(optionName) + " requires a value");
    }
    ++index;
    return argv[index];
}

Options parseArguments(int argc, char* argv[])
{
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help" || arg == "/?") {
            options.showHelp = true;
            continue;
        }

        if (arg == "-f") {
            if (options.hasFilePath) {
                fail("-f was specified more than once");
            }
            options.filePath = requireValue(argc, argv, i, "-f");
            options.hasFilePath = true;
        } else if (arg == "-s") {
            if (options.start.has_value()) {
                fail("-s was specified more than once");
            }
            options.start = parseNumber(requireValue(argc, argv, i, "-s"), "-s");
        } else if (arg == "-e") {
            if (options.end.has_value()) {
                fail("-e was specified more than once");
            }
            options.end = parseNumber(requireValue(argc, argv, i, "-e"), "-e");
        } else if (arg == "-u") {
            const auto parsed = parseNumber(requireValue(argc, argv, i, "-u"), "-u");
            if (parsed.negative) {
                fail("-u must be 8, 16, or 32");
            }
            if (parsed.magnitude != 8 && parsed.magnitude != 16 && parsed.magnitude != 32) {
                fail("-u must be 8, 16, or 32");
            }
            options.widthBits = static_cast<int>(parsed.magnitude);
        } else if (arg == "-m") {
            if (options.embed.has_value()) {
                fail("-m was specified more than once");
            }
            options.embed = parseNumber(requireValue(argc, argv, i, "-m"), "-m");
        } else if (arg == "-n") {
            const auto value = requireValue(argc, argv, i, "-n");
            if (iequals(value, "L")) {
                options.endian = Endian::Little;
            } else if (iequals(value, "B")) {
                options.endian = Endian::Big;
            } else {
                fail("-n must be L or B");
            }
        } else {
            fail("Unknown option: " + arg);
        }
    }

    if (!options.showHelp && !options.hasFilePath) {
        fail("-f is required");
    }

    return options;
}

std::uint64_t resolveNonNegativeOffset(
    const ParsedNumber& number,
    std::uint64_t fileSize,
    std::string_view optionName)
{
    if (number.negative) {
        fail(std::string(optionName) + " does not accept a negative value");
    }
    if (number.magnitude > fileSize) {
        fail(std::string(optionName) + " is beyond the end of the file");
    }
    return number.magnitude;
}

std::uint64_t resolveEndOffset(const ParsedNumber& number, std::uint64_t fileSize)
{
    if (!number.negative) {
        if (number.magnitude > fileSize) {
            fail("-e is beyond the end of the file");
        }
        return number.magnitude;
    }

    if (number.magnitude > fileSize) {
        fail("-e negative offset is larger than the file size");
    }
    return fileSize - number.magnitude;
}

std::uint64_t resolveEmbedOffset(const ParsedNumber& number, std::uint64_t fileSize, std::uint64_t byteCount)
{
    std::uint64_t offset = 0;
    if (number.negative) {
        if (number.magnitude > fileSize) {
            fail("-m negative offset is larger than the file size");
        }
        offset = fileSize - number.magnitude;
    } else {
        offset = number.magnitude;
    }

    if (offset > fileSize || byteCount > fileSize - offset) {
        fail("-m write range is beyond the end of the file");
    }
    return offset;
}

bool rangesOverlap(
    std::uint64_t firstStart,
    std::uint64_t firstEnd,
    std::uint64_t secondStart,
    std::uint64_t secondEnd)
{
    return std::max(firstStart, secondStart) < std::min(firstEnd, secondEnd);
}

ResolvedOptions resolveOptions(const Options& options)
{
    ResolvedOptions resolved;
    resolved.filePath = options.filePath;
    resolved.widthBits = options.widthBits;
    resolved.endian = options.endian;

    std::error_code error;
    if (!std::filesystem::exists(resolved.filePath, error) || error) {
        fail("File does not exist: " + resolved.filePath.string());
    }
    if (!std::filesystem::is_regular_file(resolved.filePath, error) || error) {
        fail("Path is not a regular file: " + resolved.filePath.string());
    }

    const auto size = std::filesystem::file_size(resolved.filePath, error);
    if (error) {
        fail("Could not get file size: " + error.message());
    }
    resolved.fileSize = static_cast<std::uint64_t>(size);

    resolved.start = options.start
        ? resolveNonNegativeOffset(*options.start, resolved.fileSize, "-s")
        : 0;
    resolved.endExclusive = options.end
        ? resolveEndOffset(*options.end, resolved.fileSize)
        : resolved.fileSize;

    if (resolved.endExclusive < resolved.start) {
        fail("-e resolves before -s");
    }

    const auto byteCount = static_cast<std::uint64_t>(resolved.widthBits / 8);
    if (options.embed) {
        const auto embedOffset = resolveEmbedOffset(*options.embed, resolved.fileSize, byteCount);
        if (rangesOverlap(resolved.start, resolved.endExclusive, embedOffset, embedOffset + byteCount)) {
            fail("-m write range overlaps the checksum calculation range");
        }
        resolved.embedOffset = embedOffset;
    }

    return resolved;
}

std::uint64_t maskForWidth(int widthBits)
{
    return (std::uint64_t{1} << widthBits) - 1U;
}

std::uint64_t calculateChecksum(const std::filesystem::path& filePath, std::uint64_t start, std::uint64_t endExclusive)
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        fail("Could not open file for reading: " + filePath.string());
    }

    input.seekg(static_cast<std::streamoff>(start), std::ios::beg);
    if (!input) {
        fail("Could not seek to the start offset");
    }

    std::uint64_t sum = 0;
    std::uint64_t remaining = endExclusive - start;
    std::array<char, 64 * 1024> buffer{};

    while (remaining > 0) {
        const auto chunkSize = static_cast<std::streamsize>(
            std::min<std::uint64_t>(remaining, static_cast<std::uint64_t>(buffer.size())));

        input.read(buffer.data(), chunkSize);
        const auto readCount = input.gcount();
        if (readCount <= 0) {
            fail("Could not read the requested checksum range");
        }

        for (std::streamsize i = 0; i < readCount; ++i) {
            sum += static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
        }

        remaining -= static_cast<std::uint64_t>(readCount);
    }

    return sum;
}

std::vector<unsigned char> checksumBytes(std::uint64_t checksum, int widthBits, Endian endian)
{
    const auto byteCount = static_cast<std::size_t>(widthBits / 8);
    std::vector<unsigned char> bytes(byteCount);

    for (std::size_t i = 0; i < byteCount; ++i) {
        const auto shift = static_cast<unsigned int>(8U * i);
        const auto value = static_cast<unsigned char>((checksum >> shift) & 0xffU);
        if (endian == Endian::Little) {
            bytes[i] = value;
        } else {
            bytes[byteCount - 1U - i] = value;
        }
    }

    return bytes;
}

void writeChecksum(
    const std::filesystem::path& filePath,
    std::uint64_t offset,
    const std::vector<unsigned char>& bytes)
{
    std::fstream file(filePath, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        fail("Could not open file for writing: " + filePath.string());
    }

    file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) {
        fail("Could not seek to the embed offset");
    }

    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        fail("Could not write checksum bytes");
    }
}

std::string hexValue(std::uint64_t value, int minWidth = 0)
{
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << std::setfill('0');
    if (minWidth > 0) {
        output << std::setw(minWidth);
    }
    output << value;
    return output.str();
}

const char* endianName(Endian endian)
{
    return endian == Endian::Little ? "Little" : "Big";
}

void printResult(const ResolvedOptions& options, std::uint64_t checksum)
{
    const int hexDigits = options.widthBits / 4;
    std::cout << "checksum: " << hexValue(checksum, hexDigits) << " (" << checksum << ")\n";
    std::cout << "range: [" << hexValue(options.start) << ", " << hexValue(options.endExclusive) << ")\n";
    std::cout << "width: " << options.widthBits << " bit\n";

    if (options.embedOffset) {
        std::cout << "embedded: offset " << hexValue(*options.embedOffset)
                  << ", endian " << endianName(options.endian) << "\n";
    }
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const auto options = parseArguments(argc, argv);
        if (options.showHelp) {
            std::cout << usage();
            return 0;
        }

        const auto resolved = resolveOptions(options);
        const auto rawChecksum = calculateChecksum(resolved.filePath, resolved.start, resolved.endExclusive);
        const auto checksum = rawChecksum & maskForWidth(resolved.widthBits);

        if (resolved.embedOffset) {
            writeChecksum(
                resolved.filePath,
                *resolved.embedOffset,
                checksumBytes(checksum, resolved.widthBits, resolved.endian));
        }

        printResult(resolved, checksum);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n\n" << usage();
        return 1;
    }
}
