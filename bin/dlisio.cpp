#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include <fmt/core.h>

#include <dlisio/dlisio.h>
#include <dlisio/types.h>

namespace {

constexpr std::ios_base::iostate stream_failures() noexcept (true) {
    return std::ios_base::failbit | std::ios_base::badbit;
}

long long find_sul(const std::vector< char >& buffer) noexcept (true) {
    long long offset;
    const auto err = dlis_find_sul(buffer.data(), buffer.size(), &offset);

    switch (err) {
        case DLIS_OK:
            if (offset != 0)
                std::cerr << offset << " garbage bytes before SUL\n";
            return offset;

        case DLIS_NOTFOUND:
            std::cerr << "searched " << buffer.size() << " bytes, "
                      << "but could not find SUL\n";
            std::exit(EXIT_FAILURE);

        case DLIS_INCONSISTENT:
            std::cerr << "found something that could be parts of a SUL, "
                      << "file may be corrupted"
                      ;
            std::exit(EXIT_FAILURE);

        default:
            std::cerr << "unknown error when looking for SUL\n";
            std::exit(EXIT_FAILURE);
    }
}

long long find_vrl(const std::vector< char >& buffer) noexcept (true) {
    long long offset;
    const auto err = dlis_find_vrl(buffer.data(), buffer.size(), &offset);

    switch (err) {
        case DLIS_OK:
            if (offset != 0) {
                std::cerr << offset << " garbage bytes between SUL "
                          << "and first visible envelope\n"
                          ;
            }
            return offset;

        case DLIS_NOTFOUND:
            std::cerr << "searched " << buffer.size() << " bytes, "
                      << "but could not find VRL\n";
            std::exit(EXIT_FAILURE);

        case DLIS_INCONSISTENT:
            std::cerr << "found something that could be parts of a "
                      << "visible envelope, file may be corrupted"
                      ;
            std::exit(EXIT_FAILURE);

        default:
            std::cerr << "unknown error when looking for visible envelope\n";
            std::exit(EXIT_FAILURE);
    }
}

void print_sul(const std::vector< char >& buffer) noexcept (false) {
    if (buffer.size() < DLIS_SUL_SIZE)
        throw std::runtime_error("print_envelope: buffer too small");

    int seqnum = -1;
    int major = -1;
    int minor = -1;
    int layout = -1;
    std::int64_t maxlen = -1;
    char id[61] = {};

    const auto err = dlis_sul(buffer.data(),
                              &seqnum,
                              &major,
                              &minor,
                              &layout,
                              &maxlen,
                              id);

    if (err != DLIS_OK) {
        std::cerr << "invalid SUL - not supported yet\n";
        std::exit(EXIT_FAILURE);
    }

    static const auto msg =
        "storage unit label:\n"
        "    " "sequence-number: {}\n"
        "    " "version: V{}.{}\n"
        "    " "layout: {}\n"
        "    " "id: {}\n"
    ;

    std::cout << fmt::format(msg, seqnum, major, minor, layout, id);
}

void print_envelope(const std::vector< char >& buffer) noexcept (false) {
    if (buffer.size() < DLIS_VRL_SIZE)
        throw std::runtime_error("print_envelope: buffer too small");

    int length;
    unsigned char padbyte = 0;
    int version;

    const auto err = dlis_vrl(buffer.data(), &length, &version);

    if (err != DLIS_OK) {
        std::cerr << "invalid visible envelope - not supported yet\n";
        std::exit(EXIT_FAILURE);
    }

    std::memcpy(&padbyte, buffer.data() + 2, 1);

    static const auto msg =
        "visible envelope (VRL):\n"
        "    " "length: {}\n"
        "    " "pad-byte: {:#02x}\n"
        "    " "version: {}\n"
    ;

    std::cout << fmt::format(msg, length, padbyte, version);
}

void read_back(std::ifstream& fs,
               std::vector< char >& buffer,
               std::size_t target_size)
noexcept (false) {
    const auto current_size = buffer.size();

    if (target_size < current_size)
        return;

    const auto n = target_size - current_size;
    buffer.resize(target_size);
    fs.read(buffer.data() + current_size, n);
}

}

int main(int args, char** argv) {
    if (args != 2) {
        std::cerr << "usage: dlisio FILE\n";
        std::exit(EXIT_FAILURE);
    }

    /* open file */
    /* EOF starts by being a fatal error */
    std::ifstream fs;
    fs.exceptions(stream_failures() | std::ifstream::eofbit);
    fs.open(argv[1], std::ifstream::binary);

    std::vector< char > buffer;

    /* read SUL */
    read_back(fs, buffer, 200);
    const auto sul_pos = find_sul(buffer);
    buffer.erase(buffer.begin(), buffer.begin() + sul_pos);

    /* parse & print the SUL */
    read_back(fs, buffer, DLIS_SUL_SIZE);
    print_sul(buffer);
    buffer.erase(buffer.begin(), buffer.begin() + DLIS_SUL_SIZE);

    /* now look for the VRL which may not pop up immediately */
    read_back(fs, buffer, 200);
    const auto vrl_pos = find_vrl(buffer);
    buffer.erase(buffer.begin(), buffer.begin() + vrl_pos);
    print_envelope(buffer);

    /* main loop */
    buffer.erase(buffer.begin(), buffer.begin() + DLIS_VRL_SIZE);
}
