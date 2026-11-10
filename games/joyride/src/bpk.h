#pragma once
// Joy Ride Turbo's data.bpk, read front to back.
//
// Layout: a BigPark header (0x0FF512ED, three more words), a table of big-endian block output sizes (one per 128 KB
// of file), then the blocks: block 0 right after the table, block i at i * 128 KB. Each block is an XMemCompress
// LZX stream of 32 KB frames (each prefixed by a big-endian u16 compressed size, or 0xFF + u16 output size + u16
// compressed size), or a stored block (a zero word, then the bytes). The blocks' output concatenated is a "9KPB" pack
// (read by assets_joyride.cpp). The window carries over from block to block (trees and R0-R2 restart), so the blocks
// are decoded in order; only the last 128 KB is kept.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace bpk {

// Calls fn(data, size, at) with each block's output in order (`at` = where it starts in the contents). fn returns
// false to stop early (Stream then returns true). false: the file could not be read or a block did not decode
// (err says which).
bool Stream(const std::filesystem::path& file,
            const std::function<bool(const uint8_t* data, size_t size, uint64_t at)>& fn, std::string& err);

} // namespace bpk
