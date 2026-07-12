#include "meshoptimizer.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr char kRequestMagic[8] = {'E', 'L', '2', 'D', 'M', 'O', '1', '\0'};
constexpr char kResponseMagic[8] = {'E', 'L', '2', 'D', 'M', 'R', '1', '\0'};

bool read_exact(std::istream& stream, void* output, std::size_t size) {
    stream.read(static_cast<char*>(output), static_cast<std::streamsize>(size));
    return stream.good() || static_cast<std::size_t>(stream.gcount()) == size;
}

bool read_u32(std::istream& stream, std::uint32_t& output) {
    unsigned char bytes[4] = {};
    if (!read_exact(stream, bytes, sizeof(bytes))) {
        return false;
    }
    output = static_cast<std::uint32_t>(bytes[0]) |
             (static_cast<std::uint32_t>(bytes[1]) << 8u) |
             (static_cast<std::uint32_t>(bytes[2]) << 16u) |
             (static_cast<std::uint32_t>(bytes[3]) << 24u);
    return true;
}

bool read_f32(std::istream& stream, float& output) {
    std::uint32_t bits = 0;
    if (!read_u32(stream, bits)) {
        return false;
    }
    static_assert(sizeof(bits) == sizeof(output), "unexpected float size");
    std::memcpy(&output, &bits, sizeof(output));
    return true;
}

void write_u32(std::ostream& stream, std::uint32_t value) {
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8u) & 0xffu),
        static_cast<unsigned char>((value >> 16u) & 0xffu),
        static_cast<unsigned char>((value >> 24u) & 0xffu),
    };
    stream.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void write_f32(std::ostream& stream, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(value));
    write_u32(stream, bits);
}

template <typename T>
bool checked_product(std::uint32_t left, std::uint32_t right, T& output) {
    const std::uint64_t product = static_cast<std::uint64_t>(left) * right;
    if (product > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
        return false;
    }
    output = static_cast<T>(product);
    return true;
}

int run(const std::string& request_path, const std::string& response_path) {
    std::ifstream request(request_path, std::ios::binary);
    if (!request) {
        std::cerr << "failed to open request: " << request_path << '\n';
        return 2;
    }

    char magic[8] = {};
    if (!read_exact(request, magic, sizeof(magic)) || std::memcmp(magic, kRequestMagic, sizeof(magic)) != 0) {
        std::cerr << "invalid request magic\n";
        return 3;
    }

    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::uint32_t attribute_count = 0;
    std::uint32_t target_index_count = 0;
    std::uint32_t options = 0;
    float target_error = 0.0f;
    if (!read_u32(request, vertex_count) ||
        !read_u32(request, index_count) ||
        !read_u32(request, attribute_count) ||
        !read_u32(request, target_index_count) ||
        !read_u32(request, options) ||
        !read_f32(request, target_error)) {
        std::cerr << "truncated request header\n";
        return 4;
    }
    if (vertex_count == 0u || index_count == 0u || index_count % 3u != 0u ||
        target_index_count == 0u || target_index_count > index_count || target_index_count % 3u != 0u ||
        attribute_count == 0u || attribute_count > 32u || target_error < 0.0f) {
        std::cerr << "invalid request dimensions\n";
        return 5;
    }

    std::size_t position_count = 0;
    std::size_t attribute_value_count = 0;
    if (!checked_product(vertex_count, 3u, position_count) ||
        !checked_product(vertex_count, attribute_count, attribute_value_count)) {
        std::cerr << "request dimensions overflow\n";
        return 6;
    }

    std::vector<float> positions(position_count);
    std::vector<float> attributes(attribute_value_count);
    std::vector<float> weights(attribute_count);
    std::vector<unsigned char> locks(vertex_count);
    std::vector<unsigned int> indices(index_count);
    for (float& value : positions) {
        if (!read_f32(request, value)) {
            std::cerr << "truncated positions\n";
            return 7;
        }
    }
    for (float& value : attributes) {
        if (!read_f32(request, value)) {
            std::cerr << "truncated attributes\n";
            return 8;
        }
    }
    for (float& value : weights) {
        if (!read_f32(request, value) || value < 0.0f) {
            std::cerr << "invalid attribute weights\n";
            return 9;
        }
    }
    if (!read_exact(request, locks.data(), locks.size())) {
        std::cerr << "truncated vertex locks\n";
        return 10;
    }
    for (unsigned int& index : indices) {
        std::uint32_t value = 0;
        if (!read_u32(request, value) || value >= vertex_count) {
            std::cerr << "invalid index buffer\n";
            return 11;
        }
        index = value;
    }

    std::vector<unsigned int> simplified(index_count);
    float result_error = 0.0f;
    const std::size_t result_index_count = meshopt_simplifyWithAttributes(
        simplified.data(),
        indices.data(),
        indices.size(),
        positions.data(),
        vertex_count,
        3u * sizeof(float),
        attributes.data(),
        attribute_count * sizeof(float),
        weights.data(),
        attribute_count,
        locks.data(),
        target_index_count,
        target_error,
        options,
        &result_error);
    if (result_index_count == 0u || result_index_count > index_count || result_index_count % 3u != 0u) {
        std::cerr << "meshoptimizer returned an invalid index count\n";
        return 12;
    }

    std::ofstream response(response_path, std::ios::binary | std::ios::trunc);
    if (!response) {
        std::cerr << "failed to open response: " << response_path << '\n';
        return 13;
    }
    response.write(kResponseMagic, sizeof(kResponseMagic));
    write_u32(response, static_cast<std::uint32_t>(result_index_count));
    write_f32(response, result_error);
    for (std::size_t index = 0; index < result_index_count; ++index) {
        write_u32(response, simplified[index]);
    }
    if (!response) {
        std::cerr << "failed to write response\n";
        return 14;
    }

    std::cout << "input_triangles=" << index_count / 3u
              << " target_triangles=" << target_index_count / 3u
              << " output_triangles=" << result_index_count / 3u
              << " error=" << result_error << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: el2d_mesh_simplify REQUEST.bin RESPONSE.bin\n";
        return 1;
    }
    return run(argv[1], argv[2]);
}
