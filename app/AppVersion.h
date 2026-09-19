#pragma once

#include <AppFileInfo.h>
#include <string>

// The caller supplies translations in its existing catalog context.
struct AppVersionLabels {
	std::string version = "Version";
	std::string development = "development";
	std::string alpha = "alpha";
	std::string beta = "beta";
	std::string gamma = "gamma";
	std::string goldMaster = "gold master";
};

inline std::string
AppVersionSuffix(uint32 variety, const AppVersionLabels& labels)
{
	switch (variety) {
		case B_DEVELOPMENT_VERSION: return labels.development;
		case B_ALPHA_VERSION: return labels.alpha;
		case B_BETA_VERSION: return labels.beta;
		case B_GAMMA_VERSION: return labels.gamma;
		case B_GOLDEN_MASTER_VERSION: return labels.goldMaster;
		default: return "";
	}
}

// nullptr means unavailable binary metadata. Preserve the existing all-zero
// fallback and omission of patch zero; internal/description fields are not used.
inline std::string
FormatAppVersion(const version_info* info, const std::string& fallback,
	const AppVersionLabels& labels = {})
{
	std::string text = labels.version + " ";
	if (!info || (info->major == 0 && info->middle == 0 && info->minor == 0))
		return text + fallback;
	text += std::to_string(info->major) + "." + std::to_string(info->middle);
	if (info->minor > 0)
		text += "." + std::to_string(info->minor);
	std::string suffix = AppVersionSuffix(info->variety, labels);
	if (!suffix.empty())
		text += "-" + suffix;
	return text;
}
