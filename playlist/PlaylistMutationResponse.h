#pragma once

#include "spotify/api/SpotifyApiTypes.h"

#include <cstdint>
#include <limits>

// Shared decoding for playlist write outcomes. Transport success stays explicit;
// a missing snapshot requires a metadata refresh, not reuse of the old snapshot.
namespace PlaylistMutationResponse {
inline int32_t
Status(const nlohmann::json& data)
{
	if (!data.is_object())
		return -1;
	auto status = data.find("status");
	if (status == data.end() || !status->is_number_integer())
		return -1;
	if (status->is_number_unsigned()) {
		auto value = status->get<nlohmann::json::number_unsigned_t>();
		return value <= static_cast<uint64_t>(std::numeric_limits<int32_t>::max())
			? static_cast<int32_t>(value) : -1;
	}
	auto value = status->get<nlohmann::json::number_integer_t>();
	return value >= std::numeric_limits<int32_t>::min()
		&& value <= std::numeric_limits<int32_t>::max()
		? static_cast<int32_t>(value) : -1;
}

inline std::string
SnapshotId(const nlohmann::json& data)
{
	if (!data.is_object())
		return "";
	const nlohmann::json* body = &data;
	nlohmann::json decoded;
	auto wrapped = data.find("body");
	if (wrapped != data.end() && wrapped->is_string()) {
		decoded = nlohmann::json::parse(wrapped->get<std::string>(), nullptr, false);
		if (!decoded.is_object())
			return "";
		body = &decoded;
	}
	auto snapshot = body->find("snapshot_id");
	return snapshot != body->end() && snapshot->is_string()
		? snapshot->get<std::string>() : "";
}
}
