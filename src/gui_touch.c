// https://github.com/hodgef/simple-keyboard
/*
tcspec keyboardDefault: [
  "` 1 2 3 4 5 6 7 8 9 0 - = bksp"
  "tab q w e r t y u i o p [ ] \"
  "lock a s d f g h j k l ; ' enter"
  "shift z x c v b n m , . / shift"
  "space"
]

tcspec keyboardShift: [
  "~ ! @ # $ % ^ & * ( ) _ + bksp"
  "tab Q W E R T Y U I O P { } |"
  "lock A S D F G H J K L : ^" enter"
  "shift Z X C V B N M < > ? shift"
  "space";
]
*/

#include "gui_touchLayout.c"


float* gui_keyboardQuads(float* attr, int shift)
{
    const char* str = shift ? stab_keyboardShift : stab_keyboardDefault;
    const uint8_t* bc = shift ? layout_keyboardShift : layout_keyboardDefault;
    const void* data[15*4];
    GuiArea areas[15*4];
    const void** dp = data;
    const char* cp;

    *dp++ = areas;

    for (cp = str; *cp != '\0'; ++cp) {
        //printf("%s\n", cp);
        *dp++ = cp;
        do {
            ++cp;
        } while(*cp != '\0');
    }

    TxfDrawState ds;
    ds.fontTable = screenState()->fontTable;
    return gui_layout(attr, NULL, &ds, bc, data);
}
