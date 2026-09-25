#include "ui/UiScalePolicy.h"
#include "ui/PlayerControlMetrics.h"
#include "ui/ReplicantHandleMetrics.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error UI metrics tests require assertions; compile without NDEBUG.
#endif

static void Check(bool condition) { assert(condition); }

static void
CheckFontScale()
{
	Check(UiScale::FontScale(9.0f) == 1.0f);
	Check(UiScale::FontScale(12.0f) == 1.0f);
	Check(UiScale::FontScale(15.0f) == 1.25f);
	Check(UiScale::FontScale(18.0f) == 1.5f);
	Check(UiScale::FontScale(24.0f) == 2.0f);
	Check(UiScale::Scaled(8.0f, 0.5f) == 8.0f);
	Check(UiScale::Scaled(5.0f, 1.25f) == 7.0f);
	Check(UiScale::Scaled(0.0f, 2.0f) == 0.0f);
}

static void
CheckLegacySizes()
{
	// Golden dimensions from the rules before extraction (50a4993). Font
	// measurements are explicit inputs, independent of the host's installed font.
	struct Sample {
		float scale, lineHeight, clockWidth;
		float control, button, icon, clock, inset, row, bar;
		float volumeMin, volumePreferred, volumeMax, volumeThickness, volumeTop;
		float seekThickness, seekMin, seekPreferred;
	};
	const Sample samples[] = {
		{1, 16, 28, 24, 26, 18, 36, 5, 3, 65,
			96, 120, 132, 4, 4, 18, 160, 384},
		{1.25f, 20, 35, 28, 30, 22.5f, 45, 6.25f, 4, 77.5f,
			112.5f, 140, 154, 5, 5, 22.5f, 250, 600},
		{1.5f, 24, 42, 32, 36, 27, 54, 7.5f, 5, 93,
			135, 165, 180, 6, 6, 27, 360, 864},
		{2, 32, 56, 40, 48, 36, 72, 10, 6, 124,
			180, 220, 240, 8, 8, 36, 640, 1536}
	};
	for (const auto& sample : samples) {
		const auto bar = ResolvePlayerBarMetrics(sample.scale,
			sample.lineHeight, sample.clockWidth);
		Check(bar.controlHeight == sample.control);
		Check(bar.buttonWidth == sample.button);
		Check(bar.iconSize == sample.icon);
		Check(bar.timeWidth == sample.clock);
		Check(bar.outerInset == sample.inset);
		Check(bar.rowSpacing == sample.row);
		Check(bar.barHeight == sample.bar);
		Check(bar.volumeMinWidth == sample.volumeMin);
		Check(bar.volumePreferredWidth == sample.volumePreferred);
		Check(bar.volumeMaxWidth == sample.volumeMax);
		Check(bar.volumeThickness == sample.volumeThickness);
		Check(bar.volumeTopInset == sample.volumeTop);
		const auto seek = ResolveSeekBarMetrics(sample.scale, sample.lineHeight);
		Check(seek.height == bar.controlHeight);
		Check(seek.thickness == sample.seekThickness);
		Check(seek.minWidth == sample.seekMin);
		Check(seek.preferredWidth == sample.seekPreferred);
	}
}

static void
CheckTallFontAndFractionalLayout()
{
	const auto tall = ResolvePlayerBarMetrics(1.0f, 22.0f, 65.25f);
	Check(tall.controlHeight == 30.0f);
	Check(tall.buttonWidth == 32.0f);
	Check(tall.iconSize == 22.0f);
	Check(tall.timeWidth == 74.0f);
	Check(tall.barHeight == 82.0f);
	const auto fractional = ResolvePlayerBarMetrics(1.25f, 20.0f, 35.0f);
	Check(fractional.seekMinWidth == 150.0f);
	Check(fractional.seekPreferredWidth == 300.0f);
	Check(fractional.draggerSize == 10.0f);
	Check(fractional.draggerRightInset == 10.0f);
	Check(fractional.draggerBottomInset == 3.75f);
	Check(fractional.trackInfoInset == 2.5f);
	Check(fractional.addButtonInset == 7.5f);
	const auto small = ResolvePlayerBarMetrics(0.5f, 16.0f, 28.0f);
	Check(small.controlHeight == 24.0f);
	Check(small.barHeight == 65.0f);
	Check(small.draggerSize == 8.0f);
	Check(ResolveSeekBarMetrics(0.5f, 16.0f).minWidth == 160.0f);
}

static void
CheckReplicantHandle()
{
	const auto normal = UiScale::ResolveReplicantHandleMetrics(1.0f);
	Check(normal.size == 8.0f);
	Check(normal.rightInset == 8.0f);
	Check(normal.bottomInset == 3.0f);
	const auto large = UiScale::ResolveReplicantHandleMetrics(2.0f);
	Check(large.size == 16.0f);
	Check(large.rightInset == 16.0f);
	Check(large.bottomInset == 6.0f);
	Check(UiScale::ResolveReplicantHandleMetrics(0.5f).size == 8.0f);
	const auto fractional = UiScale::ResolveReplicantHandleMetrics(1.25f);
	Check(fractional.size == 10.0f);
	Check(fractional.bottomInset == 3.75f);
	// A 220x220 artwork uses the same corner margins as the player.
	Check(UiScale::ReplicantHandleOrigin(0, 219, 7, normal.rightInset) == 204);
	Check(UiScale::ReplicantHandleOrigin(0, 219, 7, normal.bottomInset) == 209);
	Check(UiScale::ReplicantHandleOrigin(0, 219, 15, large.rightInset) == 188);
	Check(UiScale::ReplicantHandleOrigin(0, 219, 15, large.bottomInset) == 198);
	Check(UiScale::ReplicantHandleOrigin(0, 219, 9, fractional.bottomInset) == 206);
	// Tiny restored frames must not position the origin outside the top/left.
	Check(UiScale::ReplicantHandleOrigin(0, 7, 7, normal.rightInset) == 0);
	Check(UiScale::ReplicantHandleOrigin(20, 27, 7, normal.rightInset) == 20);
	Check(UiScale::ReplicantHandleOrigin(0.5f, 7.5f, 7, normal.rightInset) == 0.5f);
}

int
main()
{
	Check(UiScale::ListRowHeight(8) == 16);
	Check(UiScale::ListRowHeight(12) == 17);
	Check(UiScale::ListRowHeight(15) == 21);
	Check(UiScale::ListRowHeight(18) == 26);
	Check(UiScale::ListRowHeight(24) == 34);
	CheckFontScale();
	CheckLegacySizes();
	CheckTallFontAndFractionalLayout();
	CheckReplicantHandle();
	std::puts("UI metrics tests passed");
	return 0;
}
