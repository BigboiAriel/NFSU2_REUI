#pragma once
// NFSU2's texture packs and FEng fonts as REUI's assets::source (see core/assets.h).
#include <string>

namespace assets_nfsu2 {
void Install(const std::string& gameDir);   // before uied::Init
} // namespace assets_nfsu2
