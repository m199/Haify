#pragma once

#include "DiscoverTabPolicy.h"

#include <cstdint>
#include <map>
#include <optional>

struct DiscoverAsyncContext {
	std::string accountId;
	int64_t epoch = 0;
};

struct DiscoverAsyncToken {
	DiscoverAsyncContext context;
	int64_t request = 0;
};

enum class DiscoverAsyncDisposition { Current, Reconcile, Ignore };

struct DiscoverAsyncCompletion {
	DiscoverAsyncDisposition disposition = DiscoverAsyncDisposition::Ignore;
	int32_t tab = TAB_NONE;
};

// The window's message loop owns this scope. Reset cancels application of old
// results, not remote side effects. A late success for the same account asks
// for fresh data; its stale payload is never applied. Every result is consumed once.
class DiscoverAsyncScope {
public:
	void Reset(const std::string& account, bool audiobooksEnabled)
	{
		fContext = {account, fContext.epoch + 1};
		fAudiobooksEnabled = audiobooksEnabled;
	}
	bool Matches(const std::string& account, bool audiobooksEnabled) const
	{
		return fContext.accountId == account && fAudiobooksEnabled == audiobooksEnabled;
	}
	const DiscoverAsyncContext& Context() const { return fContext; }
	bool Accepts(const DiscoverAsyncContext& context) const
	{
		return context.accountId == fContext.accountId && context.epoch == fContext.epoch;
	}
	DiscoverAsyncToken Begin(int32_t tab)
	{
		DiscoverAsyncToken token{fContext, ++fNextRequest};
		fPending[token.request] = {token, tab};
		return token;
	}
	DiscoverAsyncCompletion Complete(const DiscoverAsyncToken& token, bool success)
	{
		auto found = fPending.find(token.request);
		if (found == fPending.end() || found->second.token.context.epoch != token.context.epoch
				|| found->second.token.context.accountId != token.context.accountId)
			return {};
		int32_t tab = found->second.tab;
		fPending.erase(found);
		if (Accepts(token.context))
			return {DiscoverAsyncDisposition::Current, tab};
		if (success && token.context.accountId == fContext.accountId)
			return {DiscoverAsyncDisposition::Reconcile, tab};
		return {};
	}

private:
	struct Pending {
		DiscoverAsyncToken token;
		int32_t tab = TAB_NONE;
	};
	DiscoverAsyncContext fContext;
	bool fAudiobooksEnabled = false;
	int64_t fNextRequest = 0;
	std::map<int64_t, Pending> fPending;
};
