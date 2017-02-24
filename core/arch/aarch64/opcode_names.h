/* This file was generated by codec.py from codec.txt. */

#ifndef OPCODE_NAMES_H
#define OPCODE_NAMES_H 1

const char *opcode_names[] = {
/*   0 */ "<invalid>",
/*   1 */ "<undecoded>",
/*   2 */ "<contd>",
/*   3 */ "<label>",
/*   4 */ "adc",
/*   5 */ "adcs",
/*   6 */ "add",
/*   7 */ "adds",
/*   8 */ "adr",
/*   9 */ "adrp",
/*  10 */ "and",
/*  11 */ "ands",
/*  12 */ "asrv",
/*  13 */ "b",
/*  14 */ "bcond",
/*  15 */ "bfm",
/*  16 */ "bic",
/*  17 */ "bics",
/*  18 */ "bl",
/*  19 */ "blr",
/*  20 */ "br",
/*  21 */ "brk",
/*  22 */ "cas",
/*  23 */ "casa",
/*  24 */ "casab",
/*  25 */ "casah",
/*  26 */ "casal",
/*  27 */ "casalb",
/*  28 */ "casalh",
/*  29 */ "casb",
/*  30 */ "cash",
/*  31 */ "casl",
/*  32 */ "caslb",
/*  33 */ "caslh",
/*  34 */ "casp",
/*  35 */ "caspa",
/*  36 */ "caspal",
/*  37 */ "caspl",
/*  38 */ "cbnz",
/*  39 */ "cbz",
/*  40 */ "ccmn",
/*  41 */ "ccmp",
/*  42 */ "clrex",
/*  43 */ "cls",
/*  44 */ "clz",
/*  45 */ "crc32b",
/*  46 */ "crc32cb",
/*  47 */ "crc32ch",
/*  48 */ "crc32cw",
/*  49 */ "crc32cx",
/*  50 */ "crc32h",
/*  51 */ "crc32w",
/*  52 */ "crc32x",
/*  53 */ "csel",
/*  54 */ "csinc",
/*  55 */ "csinv",
/*  56 */ "csneg",
/*  57 */ "dmb",
/*  58 */ "dsb",
/*  59 */ "eon",
/*  60 */ "eor",
/*  61 */ "extr",
/*  62 */ "hlt",
/*  63 */ "hvc",
/*  64 */ "isb",
/*  65 */ "ld1",
/*  66 */ "ld1r",
/*  67 */ "ld2",
/*  68 */ "ld2r",
/*  69 */ "ld3",
/*  70 */ "ld3r",
/*  71 */ "ld4",
/*  72 */ "ld4r",
/*  73 */ "ldadd",
/*  74 */ "ldadda",
/*  75 */ "ldaddab",
/*  76 */ "ldaddah",
/*  77 */ "ldaddal",
/*  78 */ "ldaddalb",
/*  79 */ "ldaddalh",
/*  80 */ "ldaddb",
/*  81 */ "ldaddh",
/*  82 */ "ldaddl",
/*  83 */ "ldaddlb",
/*  84 */ "ldaddlh",
/*  85 */ "ldar",
/*  86 */ "ldarb",
/*  87 */ "ldarh",
/*  88 */ "ldaxp",
/*  89 */ "ldaxr",
/*  90 */ "ldaxrb",
/*  91 */ "ldaxrh",
/*  92 */ "ldclr",
/*  93 */ "ldclra",
/*  94 */ "ldclrab",
/*  95 */ "ldclrah",
/*  96 */ "ldclral",
/*  97 */ "ldclralb",
/*  98 */ "ldclralh",
/*  99 */ "ldclrb",
/* 100 */ "ldclrh",
/* 101 */ "ldclrl",
/* 102 */ "ldclrlb",
/* 103 */ "ldclrlh",
/* 104 */ "ldeor",
/* 105 */ "ldeora",
/* 106 */ "ldeorab",
/* 107 */ "ldeorah",
/* 108 */ "ldeoral",
/* 109 */ "ldeoralb",
/* 110 */ "ldeoralh",
/* 111 */ "ldeorb",
/* 112 */ "ldeorh",
/* 113 */ "ldeorl",
/* 114 */ "ldeorlb",
/* 115 */ "ldeorlh",
/* 116 */ "ldnp",
/* 117 */ "ldp",
/* 118 */ "ldpsw",
/* 119 */ "ldr",
/* 120 */ "ldrb",
/* 121 */ "ldrh",
/* 122 */ "ldrsb",
/* 123 */ "ldrsh",
/* 124 */ "ldrsw",
/* 125 */ "ldset",
/* 126 */ "ldseta",
/* 127 */ "ldsetab",
/* 128 */ "ldsetah",
/* 129 */ "ldsetal",
/* 130 */ "ldsetalb",
/* 131 */ "ldsetalh",
/* 132 */ "ldsetb",
/* 133 */ "ldseth",
/* 134 */ "ldsetl",
/* 135 */ "ldsetlb",
/* 136 */ "ldsetlh",
/* 137 */ "ldsmax",
/* 138 */ "ldsmaxa",
/* 139 */ "ldsmaxab",
/* 140 */ "ldsmaxah",
/* 141 */ "ldsmaxal",
/* 142 */ "ldsmaxalb",
/* 143 */ "ldsmaxalh",
/* 144 */ "ldsmaxb",
/* 145 */ "ldsmaxh",
/* 146 */ "ldsmaxl",
/* 147 */ "ldsmaxlb",
/* 148 */ "ldsmaxlh",
/* 149 */ "ldsmin",
/* 150 */ "ldsmina",
/* 151 */ "ldsminab",
/* 152 */ "ldsminah",
/* 153 */ "ldsminal",
/* 154 */ "ldsminalb",
/* 155 */ "ldsminalh",
/* 156 */ "ldsminb",
/* 157 */ "ldsminh",
/* 158 */ "ldsminl",
/* 159 */ "ldsminlb",
/* 160 */ "ldsminlh",
/* 161 */ "ldtr",
/* 162 */ "ldtrb",
/* 163 */ "ldtrh",
/* 164 */ "ldtrsb",
/* 165 */ "ldtrsh",
/* 166 */ "ldtrsw",
/* 167 */ "ldumax",
/* 168 */ "ldumaxa",
/* 169 */ "ldumaxab",
/* 170 */ "ldumaxah",
/* 171 */ "ldumaxal",
/* 172 */ "ldumaxalb",
/* 173 */ "ldumaxalh",
/* 174 */ "ldumaxb",
/* 175 */ "ldumaxh",
/* 176 */ "ldumaxl",
/* 177 */ "ldumaxlb",
/* 178 */ "ldumaxlh",
/* 179 */ "ldumin",
/* 180 */ "ldumina",
/* 181 */ "lduminab",
/* 182 */ "lduminah",
/* 183 */ "lduminal",
/* 184 */ "lduminalb",
/* 185 */ "lduminalh",
/* 186 */ "lduminb",
/* 187 */ "lduminh",
/* 188 */ "lduminl",
/* 189 */ "lduminlb",
/* 190 */ "lduminlh",
/* 191 */ "ldur",
/* 192 */ "ldurb",
/* 193 */ "ldurh",
/* 194 */ "ldursb",
/* 195 */ "ldursh",
/* 196 */ "ldursw",
/* 197 */ "ldxp",
/* 198 */ "ldxr",
/* 199 */ "ldxrb",
/* 200 */ "ldxrh",
/* 201 */ "lslv",
/* 202 */ "lsrv",
/* 203 */ "madd",
/* 204 */ "movk",
/* 205 */ "movn",
/* 206 */ "movz",
/* 207 */ "mrs",
/* 208 */ "msr",
/* 209 */ "msub",
/* 210 */ "nop",
/* 211 */ "orn",
/* 212 */ "orr",
/* 213 */ "prfm",
/* 214 */ "prfum",
/* 215 */ "rbit",
/* 216 */ "ret",
/* 217 */ "rev",
/* 218 */ "rev16",
/* 219 */ "rev32",
/* 220 */ "rorv",
/* 221 */ "sbc",
/* 222 */ "sbcs",
/* 223 */ "sbfm",
/* 224 */ "sdiv",
/* 225 */ "sev",
/* 226 */ "sevl",
/* 227 */ "smaddl",
/* 228 */ "smc",
/* 229 */ "smsubl",
/* 230 */ "smulh",
/* 231 */ "st1",
/* 232 */ "st2",
/* 233 */ "st3",
/* 234 */ "st4",
/* 235 */ "stlr",
/* 236 */ "stlrb",
/* 237 */ "stlrh",
/* 238 */ "stlxp",
/* 239 */ "stlxr",
/* 240 */ "stlxrb",
/* 241 */ "stlxrh",
/* 242 */ "stnp",
/* 243 */ "stp",
/* 244 */ "str",
/* 245 */ "strb",
/* 246 */ "strh",
/* 247 */ "sttr",
/* 248 */ "sttrb",
/* 249 */ "sttrh",
/* 250 */ "stur",
/* 251 */ "sturb",
/* 252 */ "sturh",
/* 253 */ "stxp",
/* 254 */ "stxr",
/* 255 */ "stxrb",
/* 256 */ "stxrh",
/* 257 */ "sub",
/* 258 */ "subs",
/* 259 */ "svc",
/* 260 */ "swp",
/* 261 */ "swpa",
/* 262 */ "swpab",
/* 263 */ "swpah",
/* 264 */ "swpal",
/* 265 */ "swpalb",
/* 266 */ "swpalh",
/* 267 */ "swpb",
/* 268 */ "swph",
/* 269 */ "swpl",
/* 270 */ "swplb",
/* 271 */ "swplh",
/* 272 */ "sys",
/* 273 */ "tbnz",
/* 274 */ "tbz",
/* 275 */ "ubfm",
/* 276 */ "udiv",
/* 277 */ "umaddl",
/* 278 */ "umsubl",
/* 279 */ "umulh",
/* 280 */ "wfe",
/* 281 */ "wfi",
/* 282 */ "yield",
          "xx",
};

#endif /* OPCODE_NAMES_H */
