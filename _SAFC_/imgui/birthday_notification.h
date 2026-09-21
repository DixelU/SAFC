#pragma once

namespace safc::imgui_ui
{
// The date is captured once at launch; dismissal never changes the background.
class birthday_notification
{
public:
	birthday_notification(int year = 0, int month = 0, int day = 0);
	static birthday_notification today();
	bool active() const { return years_old_ >= 0; }
	void draw();

private:
	int years_old_ = -1;
	bool open_ = false;
	bool first_frame_ = true;
};
}
