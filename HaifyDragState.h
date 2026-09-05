#pragma once

#include <Autolock.h>
#include <Locker.h>
#include <Message.h>
#include <SupportDefs.h>

inline BLocker&
HaifyActiveDragLocker()
{
	static BLocker locker("haify drag state");
	return locker;
}


inline BMessage&
HaifyActiveDragMessageStorage()
{
	static BMessage message;
	return message;
}


inline bool&
HaifyActiveDragFlag()
{
	static bool active = false;
	return active;
}


inline int32&
HaifyActiveDragGenerationStorage()
{
	static int32 generation = 0;
	return generation;
}


inline int32
SetHaifyActiveDragMessage(const BMessage& message)
{
	BAutolock lock(&HaifyActiveDragLocker());
	HaifyActiveDragMessageStorage() = message;
	HaifyActiveDragFlag() = true;
	return ++HaifyActiveDragGenerationStorage();
}


inline bool
GetHaifyActiveDragMessage(BMessage& message)
{
	BAutolock lock(&HaifyActiveDragLocker());
	if (!HaifyActiveDragFlag())
		return false;
	message = HaifyActiveDragMessageStorage();
	return true;
}


inline void
ClearHaifyActiveDragMessage()
{
	BAutolock lock(&HaifyActiveDragLocker());
	HaifyActiveDragMessageStorage() = BMessage();
	HaifyActiveDragFlag() = false;
	++HaifyActiveDragGenerationStorage();
}


inline int32
HaifyActiveDragGeneration()
{
	BAutolock lock(&HaifyActiveDragLocker());
	return HaifyActiveDragFlag() ? HaifyActiveDragGenerationStorage() : -1;
}


inline bool
HaifyActiveDragGenerationMatches(int32 generation)
{
	BAutolock lock(&HaifyActiveDragLocker());
	return HaifyActiveDragFlag()
		&& HaifyActiveDragGenerationStorage() == generation;
}
