#pragma once
// D3D objects released at the start of the next frame, not mid-frame: an ImGui draw list built earlier in the same
// frame may still point at a texture, and it's only used when the frame is rendered.
#include <unknwn.h>
#include <vector>

namespace deferfree {
inline std::vector<IUnknown*>& Queue() { static std::vector<IUnknown*> q; return q; }
inline void Later(IUnknown* p) { if (p) Queue().push_back(p); }
inline void Flush() { for (IUnknown* p : Queue()) p->Release(); Queue().clear(); }   // frame start / device going away
} // namespace deferfree
