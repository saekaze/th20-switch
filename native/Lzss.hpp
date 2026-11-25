// TH20's score / replay LZSS encoder (upstream src/progress_state/compression.cpp,
// LzssEncoder::encode), the same algorithm run natively: same output bytes and
// the same final dictionary, which the game shares with its archive decoder.
// In the module it writes bit by bit into a std::vector; a score file save
// took ~35 ms on desktop and several hundred on the Switch. The string
// comparisons read a private copy of the dictionary whose first bytes repeat
// past its end, so they compare eight bytes at a time without wrapping; the
// matches found, and so the output, are the same.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace th20n::lzss {
struct Node { std::uint32_t parent, left, right; };

class Encoder {
public:
    explicit Encoder(std::uint8_t* dictionary) : dict_(dictionary) {}
    std::vector<std::uint8_t> encode(const std::uint8_t* input, std::size_t size) {
        std::fill(dict_, dict_ + 8192, 0); ring_.fill(0); tree_.fill({0, 0, 0});
        std::size_t position = std::min<std::size_t>(18, size);
        for (std::size_t i = 0; i < position; ++i) put(std::uint32_t(i + 1), input[i]);
        tree_[8192].right = 1; tree_[1] = {8192, 0, 0};
        std::vector<std::uint8_t> output; output.reserve(size * 2);
        unsigned accumulated = 0, bits = 0;
        const auto emit = [&](std::uint32_t value, unsigned count) {
            while (count) {
                --count; accumulated = (accumulated << 1) | ((value >> count) & 1);
                if (++bits == 8) { output.push_back(std::uint8_t(accumulated)); bits = 0; accumulated = 0; }
            }
        };
        unsigned buffered = unsigned(position), length = 0; std::uint32_t cursor = 1, match = 0;
        while (buffered) {
            length = std::min(length, buffered); unsigned consumed;
            if (length < 3) { emit(1, 1); emit(dict_[cursor], 8); consumed = 1; }
            else { emit(0, 1); emit(match, 13); emit(length - 3, 4); consumed = length; }
            for (unsigned i = 0; i < consumed; ++i) {
                const auto incoming = (cursor + 18) & 8191; remove(incoming);
                if (position < size) put(incoming, input[position++]); else --buffered;
                cursor = (cursor + 1) & 8191; if (buffered) length = insert(cursor, match);
            }
        }
        emit(0, 14);
        return output;
    }
private:
    std::uint8_t* dict_;
    std::array<std::uint8_t, 8192 + 24> ring_{};   // dict_, with its first 24 bytes again at the end
    std::array<Node, 8193> tree_{};
    void put(std::uint32_t at, std::uint8_t value) { dict_[at] = value; ring_[at] = value; if (at < 24) ring_[8192 + at] = value; }
    // Length of the common prefix of the strings at a and b (at most 18) and
    // the sign of their first differing byte, as the byte-by-byte loop gives.
    unsigned common(std::uint32_t a, std::uint32_t b, int& comparison) const {
        const std::uint8_t* p = ring_.data() + a; const std::uint8_t* q = ring_.data() + b;
        for (unsigned at = 0; at < 16; at += 8) {
            std::uint64_t x, y; std::memcpy(&x, p + at, 8); std::memcpy(&y, q + at, 8);
            if (x != y) { const unsigned k = at + unsigned(__builtin_ctzll(x ^ y) >> 3); comparison = int(p[k]) - int(q[k]); return k; }   // little endian
        }
        for (unsigned k = 16; k < 18; ++k) if (p[k] != q[k]) { comparison = int(p[k]) - int(q[k]); return k; }
        comparison = 0; return 18;
    }
    void promote(std::uint32_t old_node, std::uint32_t new_node) {
        auto& old = tree_[old_node]; auto& next = tree_[new_node]; next.parent = old.parent;
        auto& parent = tree_[old.parent]; if (parent.right == old_node) parent.right = new_node; else parent.left = new_node;
        old.parent = 0;
    }
    void replace(std::uint32_t old_node, std::uint32_t new_node) {
        auto& parent = tree_[tree_[old_node].parent]; if (parent.left == old_node) parent.left = new_node; else parent.right = new_node;
        tree_[new_node] = tree_[old_node]; tree_[tree_[new_node].left].parent = new_node; tree_[tree_[new_node].right].parent = new_node;
        tree_[old_node].parent = 0;
    }
    void remove(std::uint32_t index) {
        auto& node = tree_[index]; if (!node.parent) return;
        if (!node.right) promote(index, node.left);
        else if (!node.left) promote(index, node.right);
        else { auto previous = node.left; while (tree_[previous].right) previous = tree_[previous].right; remove(previous); replace(index, previous); }
    }
    unsigned insert(std::uint32_t index, std::uint32_t& match) {
        if (!index) return 0;
        auto cursor = tree_[8192].right; unsigned longest = 0;
        for (;;) {
            int comparison = 0; const unsigned length = common(index, cursor, comparison);
            if (longest <= length) { longest = length; match = cursor; if (length >= 18) { replace(cursor, index); return length; } }
            auto& child = comparison < 0 ? tree_[cursor].left : tree_[cursor].right;
            if (child) { cursor = child; continue; }
            child = index; tree_[index] = {cursor, 0, 0}; return longest;
        }
    }
};
}
