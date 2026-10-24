#pragma once
#include <algorithm>
namespace vc {
// Native searchlight fades from full at 40 m to zero at 60 m.
inline float HeliSearchlight(float distance) {return std::max(0.f,std::min(1.f,(60.f-distance)/20.f));}
}
