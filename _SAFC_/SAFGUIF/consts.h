#define TT_UNSPECIFIED 0b0
#define TT_INPUT_FIELD 0b1
#define TT_MOVEABLE_WINDOW 0b10
#define TT_BUTTON 0b100
#define TT_TEXTBOX 0b1000
#define TT_SELPROPLIST 0b10000
#define TT_CHECKBOX 0b100000
#define TT_GRAPH 0b1000000
#define TT_EDITBOX 0b10000000

enum _TellType {
	unspecified = TT_UNSPECIFIED,
	input_field = TT_INPUT_FIELD,
	moveable_window = TT_MOVEABLE_WINDOW,
	button = TT_BUTTON,
	textbox = TT_TEXTBOX,
	selectable_properted_list = TT_SELPROPLIST,
	checkbox = TT_CHECKBOX,
	graph = TT_GRAPH,
	editbox = TT_EDITBOX
};

#define GLOBAL_LEFT 0b0001
#define GLOBAL_RIGHT 0b0010
#define GLOBAL_TOP 0b0100
#define GLOBAL_BOTTOM 0b1000

enum _Positioning {
	vertical=0b1,
	horizontal=0b10
};
enum _Align{
	center=0,
	left=GLOBAL_LEFT,
	right=GLOBAL_RIGHT,
	top=GLOBAL_TOP,
	bottom=GLOBAL_BOTTOM
};