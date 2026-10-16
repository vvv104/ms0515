/*
 * Volume.hpp - the system's file system read off an image on the host, for
 * the tests: a second reading of kernel/fs.h's format, written apart from
 * tools/mkfs.py, so that each checks the other.
 */
#pragma once

#include "Machine.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace unixos {

class Volume {
public:
    explicit Volume(std::vector<uint8_t> image) : image_(std::move(image)) {}

    unsigned word(int block, int at) const
    {
        const std::size_t o = blockOffset(block) + static_cast<std::size_t>(at);
        return image_[o] | image_[o + 1] << 8;
    }
    /* A long: the high word first. */
    unsigned long longAt(int block, int at) const
    {
        return static_cast<unsigned long>(word(block, at)) << 16 | word(block, at + 2);
    }

    struct Inode {
        unsigned mode = 0, nlink = 0, nextents = 0;
        unsigned long size = 0;
        unsigned start[5] = {}, len[5] = {};
    };

    Inode inode(unsigned n) const
    {
        const int block = static_cast<int>(word(1, 16) + (n - 1) / 16);
        const int at = static_cast<int>((n - 1) % 16 * 32);
        Inode i;
        i.mode = word(block, at);
        i.nlink = image_[blockOffset(block) + static_cast<std::size_t>(at) + 2];
        i.nextents = image_[blockOffset(block) + static_cast<std::size_t>(at) + 3];
        i.size = longAt(block, at + 4);
        for (int e = 0; e < 5; ++e) {
            i.start[e] = word(block, at + 12 + e * 4);
            i.len[e] = word(block, at + 14 + e * 4);
        }
        return i;
    }

    std::string content(const Inode &i) const
    {
        std::string s;
        for (unsigned e = 0; e < i.nextents && e < 5; ++e)
            for (unsigned b = 0; b < i.len[e]; ++b) {
                const std::size_t o = blockOffset(static_cast<int>(i.start[e] + b));
                s.append(reinterpret_cast<const char *>(&image_[o]), 512);
            }
        s.resize(static_cast<std::size_t>(i.size));
        return s;
    }

    /* A directory's names and inodes. */
    std::vector<std::pair<std::string, unsigned>> entries(unsigned dir) const
    {
        std::vector<std::pair<std::string, unsigned>> out;
        const std::string body = content(inode(dir));
        for (std::size_t o = 0; o + 16 <= body.size(); o += 16) {
            const unsigned ino = static_cast<uint8_t>(body[o]) | static_cast<uint8_t>(body[o + 1]) << 8;
            if (!ino) continue;
            std::string name = body.substr(o + 2, 14);
            name.resize(name.find('\0') == std::string::npos ? 14 : name.find('\0'));
            out.emplace_back(name, ino);
        }
        return out;
    }

    /* The inode of path from the root. */
    std::optional<unsigned> lookup(const std::string &path) const
    {
        unsigned n = 1;
        std::size_t at = 0;
        while (at < path.size()) {
            while (at < path.size() && path[at] == '/') ++at;
            if (at == path.size()) break;
            const std::size_t end = path.find('/', at);
            const std::string part = path.substr(at, end == std::string::npos ? std::string::npos : end - at);
            std::optional<unsigned> next;
            for (const auto &[name, ino] : entries(n))
                if (name == part) next = ino;
            if (!next) return std::nullopt;
            n = *next;
            at = end == std::string::npos ? path.size() : end;
        }
        return n;
    }

private:
    std::vector<uint8_t> image_;
};

} // namespace unixos
