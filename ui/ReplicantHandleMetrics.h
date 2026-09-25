#pragma once

#include <algorithm>
#include <cmath>

namespace UiScale {

struct ReplicantHandleMetrics {
	float size;
	float rightInset;
	float bottomInset;
};

inline ReplicantHandleMetrics
ResolveReplicantHandleMetrics(float scale)
{
	scale = std::max(1.0f, scale);
	return {8.0f * scale, 8.0f * scale, 3.0f * scale};
}

// Bounds and handle extent use BRect's inclusive-coordinate convention.
// Keep the handle's origin inside the view when its preferred margin cannot fit.
inline float
ReplicantHandleOrigin(float start, float end, float extent, float inset)
{
	return std::max(start, std::floor(end - extent - inset));
}

} // namespace UiScale
