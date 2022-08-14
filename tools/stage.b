#!/usr/bin/boron -s
; Stage code generator.

usage: {{
Usage: stage [-a <name> [-s] | -h | -i | --version] [<stages-file>]

Options:
  -a <name>    Add a new stage.
  -h           Print this help and exit.
  -i           Generate ID header.
  -s           Print method stubs for new stage.
  --version    Print version and exit.
}}

header-file: %src/stage_id.h
stages-file: %src/stages_u4.cpp
oper: none
stub: false

fatal: func ['code msg] [
	print msg
	quit/return select [
		usage	64  ; EX_USAGE
		data	65  ; EX_DATAERR
		noinput	66  ; EX_NOINPUT
		config	78  ; EX_CONFIG
	] code
]

generate-ids: func [source string!] [
	hdr: make string! 1024
	enum: make block! 40
	alpha-num: charset "0-9A-Z_"
	parse/case source [
		thru "const StageDef " thru '{'
		some [
			to " STAGE_" tok: 7 skip some alpha-num :tok (
				append enum next tok
			)
		]
	]

	appair hdr "// Header generated by tools/stage.b^/"
			   "^/enum StageId {^/"
	forall enum [
		append hdr reduce ["    " first enum ",^/"]
	]
	append hdr "    STAGE_COUNT^/};^/"
]

gen_id_header: does [
	; Write header to stdout.
	print generate-ids read/text stages-file
]

add_stage: does [
	alpha-num: charset "0-9A-Za-z"
	ifn parse name [some alpha-num] [
		fatal config "Invalid stage name"
	]

	source: read/text to-file stages-file
	array-end: none
	parse source [
		to "#define EXTERN___D" thru "^/^/"
		some [
			"EXTERN_" thru '^/' extern:
		]
		thru "const StageDef " to "^/};" array-end:
	]
	ifn array-end [
		fatal data join "No StageDef array found in " stages-file
	]

	lname: lowercase name
	uname: uppercase copy name
	array-ent: format [
			"^/  {NULL,^/"
		-10 "_enter, "
		 -8 "_leave, "
		 -8 "_dispatch, "
			"CHILD, 0}, // STAGE_" 0
	][
		lname lname lname uname
	]

	write stages-file
	;print
	new-source: rejoin [
		slice source extern
		"EXTERN_ELD(" lname ")^/"
		slice extern array-end
		array-ent
		array-end
	]

	write header-file generate-ids new-source

	if stub [
		print construct {{
			/*
			 * STAGE_@
			 */
			void* $_enter(Stage* st, void* args) {
			    return args;
			}
			void $_leave(Stage* st) {
			}
			int $_dispatch(Stage* st, const InputEvent* ev) {
			    return 0;
			}
		}}
		['$' lname '@' uname]
	]
]

forall args [
	switch first args [
		"-a" [oper: :add_stage name: second ++ args]
		"-h" [print usage quit]
		"-i" [oper: :gen_id_header]
		"-s" [stub: true]
		"--version" [print "stage 0.1.0" quit]
		[stages-file: first args]
	]
]

ifn exists? stages-file [
	fatal usage "Missing stages-file"
]

do oper
