#pragma once

#include <cstdint>

// Page order and button rules of the Spotify setup assistant, without a
// window. The assistant only enables what these rules allow.
enum class SetupAssistantPage : int32_t {
	Welcome = 0,
	OpenDashboard,
	CreateApp,
	EnterClientId,
	Finish,
	Count
};

class SetupAssistantSteps {
public:
	SetupAssistantPage	Page() const { return fPage; }
	int32_t				Index() const { return static_cast<int32_t>(fPage); }

	bool				CanGoBack() const { return Index() > 0; }
	// The Client ID page is the only gate: an invalid ID cannot move on.
	bool				CanGoNext(bool clientIdValid) const
	{
		if (IsLast())
			return false;
		return fPage != SetupAssistantPage::EnterClientId || clientIdValid;
	}
	bool				IsLast() const
	{
		return fPage == SetupAssistantPage::Finish;
	}
	bool				CanFinish(bool clientIdValid) const
	{
		return IsLast() && clientIdValid;
	}

	bool				Back()
	{
		if (!CanGoBack())
			return false;
		fPage = static_cast<SetupAssistantPage>(Index() - 1);
		return true;
	}
	bool				Next(bool clientIdValid)
	{
		if (!CanGoNext(clientIdValid))
			return false;
		fPage = static_cast<SetupAssistantPage>(Index() + 1);
		return true;
	}

private:
	SetupAssistantPage	fPage = SetupAssistantPage::Welcome;
};
