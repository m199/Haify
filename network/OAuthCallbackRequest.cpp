#include "OAuthCallbackRequest.h"

#include <cstdlib>
#include <map>

namespace {

std::string
UrlDecode(const std::string& value)
{
	std::string decoded;
	decoded.reserve(value.size());
	for (size_t i = 0; i < value.size(); i++) {
		if (value[i] == '+') {
			decoded += ' ';
		} else if (value[i] == '%' && i + 2 < value.size()) {
			char hex[3] = {value[i + 1], value[i + 2], 0};
			char* end = nullptr;
			long byte = strtol(hex, &end, 16);
			if (end && *end == 0) {
				decoded += (char)byte;
				i += 2;
			} else {
				decoded += value[i];
			}
		} else {
			decoded += value[i];
		}
	}
	return decoded;
}


std::map<std::string, std::string>
ParseQuery(const std::string& query)
{
	std::map<std::string, std::string> values;
	size_t start = 0;
	while (start <= query.size()) {
		size_t end = query.find('&', start);
		std::string part = query.substr(start,
			end == std::string::npos ? std::string::npos : end - start);
		size_t equals = part.find('=');
		std::string key = UrlDecode(part.substr(0, equals));
		std::string value = equals == std::string::npos
			? "" : UrlDecode(part.substr(equals + 1));
		if (!key.empty())
			values[key] = value;
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	return values;
}


bool
ParseRequestLine(const std::string& request, std::string& method,
	std::string& target)
{
	size_t lineEnd = request.find("\r\n");
	std::string line = request.substr(0, lineEnd);
	size_t firstSpace = line.find(' ');
	size_t secondSpace = firstSpace == std::string::npos
		? std::string::npos : line.find(' ', firstSpace + 1);
	if (firstSpace == std::string::npos || secondSpace == std::string::npos)
		return false;

	method = line.substr(0, firstSpace);
	target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
	return true;
}


std::string
FindQueryValue(const std::map<std::string, std::string>& params,
	const char* key)
{
	auto found = params.find(key);
	return found == params.end() ? "" : found->second;
}

}


OAuthCallbackRequest
ParseOAuthCallbackRequest(const std::string& request)
{
	OAuthCallbackRequest result;
	std::string method;
	std::string target;
	if (!ParseRequestLine(request, method, target))
		return result;

	size_t question = target.find('?');
	std::string path = target.substr(0, question);
	if (method != "GET" || path != "/callback")
		return result;

	std::map<std::string, std::string> params = question == std::string::npos
		? std::map<std::string, std::string>()
		: ParseQuery(target.substr(question + 1));
	result.isCallback = true;
	result.code = FindQueryValue(params, "code");
	result.state = FindQueryValue(params, "state");
	result.error = FindQueryValue(params, "error");
	if (result.code.empty() && result.error.empty())
		result.error = "missing_authorization_code";
	return result;
}
