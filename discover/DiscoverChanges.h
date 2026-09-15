#pragma once

#include <string>

enum class DiscoverChangeOperation { Invalid, Add, Remove, Rename };

inline const char*
DiscoverOperationName(DiscoverChangeOperation operation)
{
	switch (operation) {
		case DiscoverChangeOperation::Add: return "add";
		case DiscoverChangeOperation::Remove: return "remove";
		case DiscoverChangeOperation::Rename: return "rename";
		default: return "";
	}
}

inline DiscoverChangeOperation
DiscoverOperation(const std::string& name)
{
	if (name == "add") return DiscoverChangeOperation::Add;
	if (name == "remove") return DiscoverChangeOperation::Remove;
	if (name == "rename") return DiscoverChangeOperation::Rename;
	return DiscoverChangeOperation::Invalid;
}
