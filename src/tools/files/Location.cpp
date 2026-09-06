/*
 * Location.cpp — a device's volume through ms0515_disk: the listing from
 * the directory, every change written back to the image file at once.
 */
#include "Location.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace ms0515::files {

namespace {

constexpr int kHomeBlock = 1;
constexpr int kVolumeIdOffset = 0730;   /* octal, as RT-11 documents the home block */
constexpr int kOwnerOffset    = 0744;

std::string trimmed(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

bool rad50Char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$';
}

/* The name's two parts, or nullopt when it is not NAME[.EXT]. */
std::optional<std::pair<std::string, std::string>> splitName(const std::string &name)
{
    const auto dot = name.find('.');
    const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
    const std::string ext  = dot == std::string::npos ? "" : name.substr(dot + 1);
    if (stem.empty() || stem.size() > 6 || ext.size() > 3) return std::nullopt;
    if (ext.find('.') != std::string::npos) return std::nullopt;
    for (const char c : stem) if (!rad50Char(c)) return std::nullopt;
    for (const char c : ext) if (!rad50Char(c)) return std::nullopt;
    return std::make_pair(stem, ext);
}

} // namespace

std::string Device::label() const
{
    return name + " " + image.filename().string();
}

std::optional<Location> Location::open(const Device &device)
{
    std::ifstream in(device.image, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto image = disk::openVolume(std::move(bytes), device.spec.vol, device.spec.side);
    if (!image) return std::nullopt;
    Location loc;
    loc.device_ = device;
    loc.image_ = std::move(image);
    return loc;
}

bool Location::hasDirectory() const noexcept
{
    return image_ && image_->hasDirectory;
}

std::string Location::title() const
{
    return device_.label();
}

std::string Location::summary() const
{
    if (!hasDirectory()) return "no RT-11 directory - F9 initialises the volume";
    int files = 0, freeBlocks = 0;
    for (const auto &e : image_->directory.entries) {
        if (e.isPermanent()) ++files;
        else if (e.isEmpty()) freeBlocks += e.length;
    }
    return fmt::format("{} files, {} blocks free", files, freeBlocks);
}

std::string Location::volumeId() const
{
    if (!hasDirectory()) return "";
    const auto home = image_->block(kHomeBlock);
    if (home.size() < static_cast<size_t>(kOwnerOffset + 12)) return "";
    const std::string id(reinterpret_cast<const char *>(home.data() + kVolumeIdOffset), 12);
    const std::string owner(reinterpret_cast<const char *>(home.data() + kOwnerOffset), 12);
    const std::string a = trimmed(id), b = trimmed(owner);
    return b.empty() ? a : a + " / " + b;
}

std::vector<Entry> Location::list() const
{
    std::vector<Entry> out;
    if (!hasDirectory()) return out;
    for (const auto &e : image_->directory.permanentFiles()) {
        Entry entry;
        entry.name = e.name;
        entry.blocks = e.length;
        entry.bytes = static_cast<uint64_t>(e.length) * disk::kBlock;
        entry.date = dateText(e.date);
        entry.protectedFlag = (e.status & disk::kStatusProtected) != 0;
        out.push_back(std::move(entry));
    }
    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.name < b.name; });
    return out;
}

std::optional<Entry> Location::find(const std::string &name) const
{
    for (auto &e : list())
        if (e.name == name) return e;
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> Location::read(const std::string &name) const
{
    if (!hasDirectory() || !image_->directory.find(name)) return std::nullopt;
    return image_->readFile(name);
}

std::string Location::write(const std::string &name, std::span<const uint8_t> data, const FileMeta &meta)
{
    if (!hasDirectory()) return "no RT-11 directory on " + device_.name;
    if (!validName(name)) return name + ": not an RT-11 name (NAME.EXT, six and three letters, digits, $)";
    try {
        if (image_->directory.find(name)) disk::removeFile(image_->data, image_->side, image_->ds, name, image_->vol);
        disk::PutOptions opts;
        opts.date = dateWord(meta.date);
        opts.readOnly = meta.protectedFlag;
        disk::putFile(image_->data, image_->side, image_->ds, name, data, opts, image_->vol);
    } catch (const std::exception &e) {
        return name + ": " + e.what();
    }
    return save();
}

std::string Location::remove(const std::string &name)
{
    if (!hasDirectory()) return "no RT-11 directory on " + device_.name;
    if (!image_->directory.find(name)) return name + ": no such file";
    try {
        disk::removeFile(image_->data, image_->side, image_->ds, name, image_->vol);
    } catch (const std::exception &e) {
        return name + ": " + e.what();
    }
    return save();
}

std::string Location::rename(const std::string &name, const std::string &newName)
{
    if (!hasDirectory()) return "no RT-11 directory on " + device_.name;
    if (!image_->directory.find(name)) return name + ": no such file";
    if (!validName(newName)) return newName + ": not an RT-11 name";
    if (image_->directory.find(newName)) return newName + ": already there";
    try {
        disk::renameFile(image_->data, image_->side, image_->ds, name, newName, image_->vol);
    } catch (const std::exception &e) {
        return name + ": " + e.what();
    }
    return save();
}

std::string Location::setProtected(const std::string &name, bool on)
{
    if (!hasDirectory()) return "no RT-11 directory on " + device_.name;
    if (!image_->directory.find(name)) return name + ": no such file";
    try {
        disk::setProtected(image_->data, image_->side, image_->ds, name, on, image_->vol);
    } catch (const std::exception &e) {
        return name + ": " + e.what();
    }
    return save();
}

std::string Location::setDate(const std::string &name, const std::string &date)
{
    if (!hasDirectory()) return "no RT-11 directory on " + device_.name;
    if (!image_->directory.find(name)) return name + ": no such file";
    if (!date.empty() && dateWord(date) == 0) return date + ": not a date (YYYY-MM-DD, 1972..2099)";
    try {
        disk::setEntryDate(image_->data, image_->side, image_->ds, name, dateWord(date), image_->vol);
    } catch (const std::exception &e) {
        return name + ": " + e.what();
    }
    return save();
}

std::string Location::squeeze()
{
    if (!hasDirectory()) return "no RT-11 directory on " + device_.name;
    try {
        disk::squeeze(image_->data, image_->side, image_->ds, image_->vol);
    } catch (const std::exception &e) {
        return std::string("squeeze: ") + e.what();
    }
    return save();
}

std::string Location::init(const disk::InitOptions &opts)
{
    if (!image_) return "no image";
    try {
        disk::initVolume(image_->data, image_->side, image_->ds, opts, image_->vol);
    } catch (const std::exception &e) {
        return std::string("init: ") + e.what();
    }
    return save();
}

bool Location::reload()
{
    auto fresh = open(device_);
    if (!fresh) return false;
    image_ = std::move(fresh->image_);
    return true;
}

std::string Location::save() const
{
    {
        std::ofstream out(device_.image, std::ios::binary | std::ios::trunc);
        if (!out) return "cannot write " + device_.image.string();
        out.write(reinterpret_cast<const char *>(image_->data.data()),
                  static_cast<std::streamsize>(image_->data.size()));
        if (!out) return "cannot write " + device_.image.string();
    }
    /* re-parse: the directory the library keeps must follow the bytes */
    auto reread = disk::openVolume(image_->data, device_.spec.vol, device_.spec.side);
    if (!reread) return "the image no longer parses after the write";
    const_cast<Location *>(this)->image_ = std::move(reread);
    return "";
}

bool Location::validName(const std::string &name)
{
    return splitName(name).has_value();
}

std::string Location::toVolumeName(const std::string &hostName)
{
    std::string stem, ext;
    const auto dot = hostName.rfind('.');
    const std::string rawStem = dot == std::string::npos ? hostName : hostName.substr(0, dot);
    const std::string rawExt  = dot == std::string::npos ? "" : hostName.substr(dot + 1);
    for (const char c : rawStem) {
        const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (rad50Char(u) && stem.size() < 6) stem.push_back(u);
    }
    for (const char c : rawExt) {
        const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (rad50Char(u) && ext.size() < 3) ext.push_back(u);
    }
    if (stem.empty()) stem = "NONAME";
    return ext.empty() ? stem : stem + "." + ext;
}

uint16_t Location::dateWord(const std::string &date)
{
    int y = 0, m = 0, d = 0;
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return 0;
    try {
        y = std::stoi(date.substr(0, 4));
        m = std::stoi(date.substr(5, 2));
        d = std::stoi(date.substr(8, 2));
        return disk::encodeDate(y, m, d);
    } catch (const std::exception &) {
        return 0;
    }
}

std::string Location::dateText(uint16_t word)
{
    if (word == 0) return "";
    const auto dp = disk::decodeDate(word);
    return fmt::format("{:04}-{:02}-{:02}", dp.year, dp.month, dp.day);
}

std::optional<std::vector<uint8_t>> readHostFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string writeHostFile(const std::filesystem::path &path, std::span<const uint8_t> data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return "cannot write " + path.string();
    out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    return out ? "" : "cannot write " + path.string();
}

} /* namespace ms0515::files */
