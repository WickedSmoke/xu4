#!/usr/bin/boron -s

src: read/text first args

/*
; Read Layout Opcodes.
opcodes: []
space: charset "^9^/ "
alpha: charset "A-Z_"
code: 0
parse read/text "src/gui.h" [
    thru "GuiOpcode {"
    some [
        some space
      | tok: some alpha :tok thru '^/' (
            appair opcodes mark-sol to-set-word lowercase tok ++ code
        )
      | '/' thru '^/'
    ]
]
opcodes: context opcodes
;probe opcodes
*/

; Gather layout specs from src file.
specs: []
parse src [some[
    to "^/tcspec" skip tok: thru "]^/" :tok (append specs to-block tok)
]]
if empty? specs [
    print "No touch control specification (tcspec) found"
    quit/return 1
]

emit-code: func [blk] [
    foreach it blk [
        either eq? 'cr it [
            prin "^/    "
        ][
            prin it
            prin ", "
        ]
    ]
]

char-array: func [str] [
    out: make string! 256
    foreach char str [
        appair out to-hex char ','
    ]
    out
]

print {{
// Generated by tools/gui-layout.b

#define LAYOUT_KV   LAYOUT_V, SPACING_PER, 2
#define LAYOUT_KH   LAYOUT_H, SPACING_PER, 1, FIX_WIDTH_EM, 20
#define BT          BUTTON_DT_S, STORE_AREA
}}

foreach [tcspec name blk] specs [
    strings: make string! 256
   ;soff: make block! 60

    prin construct "static const uint8_t layout_@[] = {^/    " ['@' name]
    emit-code [LAYOUT_KV ARRAY_DT_AREA 0]
    foreach str blk [
        emit-code [cr LAYOUT_KH]

        line: terminate str ' '
        bcount: 0
        foreach it line [if eq? ' ' it [++ bcount]]
        loop bcount [emit-code [BT /*BUTTON_DT_S*/]]

        append strings construct line [' ' '^0']
        emit-code [LAYOUT_END]
    ]
    emit-code [cr LAYOUT_END]
    print "^/};"

    prin rejoin ["static const char stab_" name "[] = {^/    "]
    prin char-array strings
    print "0^/};^/"

   ;print rejoin [
   ;    {static const uint16_t soff_} name { = ^{^/    "} soff {"^};^/}
   ;]
]
