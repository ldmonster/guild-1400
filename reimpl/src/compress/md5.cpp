#include "compress/md5.h"

#include <cstring>

namespace guild::compress {

namespace {

// Left-rotate, matching the original's "(x >> (32-n)) | (x << n)" idiom.
inline u32 Rotl(u32 x, int n) {
    return (x >> (32 - n)) | (x << n);
}

// VIBE_Md5_Transform @0x1418e80 — one 64-byte block. a1 = state[4]+count[2],
// a2 = the 16-word message block. Translated 1:1 from the Hex-Rays pseudocode;
// every additive constant is the exact 32-bit value the decompiler printed as a
// signed int (intentional 32-bit wraparound). The nonlinear functions are kept
// verbatim from the decompilation:
//   round 1 (F): ((d ^ c) & b) ^ d
//   round 2 (G): ((b ^ c) & d) ^ c   (written from each step's operands)
//   round 3 (H): x ^ y ^ z
//   round 4 (I): (~x | z) ^ y
void Md5Transform(u32* a1, const u32* a2) {
    u32 v69 = a1[1];
    u32 v36 = a1[2];
    u32 v3  = a1[3];
    u32 v101, v102, v37, v38, v70, v71, v103, v104;
    u32 v4, v5, v6, v7, v8, v9, v10, v11;
    u32 v39, v40, v72, v73, v105, v106, v41, v42;
    u32 v74, v75, v107, v108, v43, v44, v76, v77;
    u32 v109, v110, v12, v13, v45, v46, v78, v79;
    u32 v111, v112, v14, v15, v47, v48, v80, v81;
    u32 v113, v114, v16, v17, v49, v50, v82, v83;
    u32 v115, v116, v18, v19, v51, v52, v84, v85;
    u32 v117, v118, v20, v21, v53, v54, v86, v87;
    u32 v119, v120, v22, v23, v55, v56, v88, v89;
    u32 v121, v122, v24, v25, v57, v58, v90, v91;
    u32 v123, v124, v26, v27, v59, v60, v92, v93;
    u32 v125, v126, v28, v29, v61, v62, v94, v95;
    u32 v127, v128, v30, v31, v63, v64, v96, v97;
    u32 v129, v130, v32, v33, v65, v66, v98, v99;
    u32 v131, v132, v34, v35, v67, v68, v100;

    // --- Round 1 (F) ---
    v101 = a1[0] + a2[0]  + ((((v3 ^ v36) & v69)) ^ v3)   - 680876936u;
    v102 = v69 + Rotl(v101, 7);
    v4   = v3  + a2[1]  + ((((v36 ^ v69) & v102)) ^ v36)  - 389564586u;
    v5   = v102 + Rotl(v4, 12);
    v37  = v36 + a2[2]  + ((((v69 ^ v102) & v5)) ^ v69)   + 606105819u;
    v38  = v5  + Rotl(v37, 17);
    v70  = v69 + a2[3]  + ((((v102 ^ v5) & v38)) ^ v102)  - 1044525330u;
    v71  = v38 + Rotl(v70, 22);
    v103 = v102 + a2[4] + ((((v5 ^ v38) & v71)) ^ v5)     - 176418897u;
    v104 = v71 + Rotl(v103, 7);
    v6   = v5  + a2[5]  + ((((v38 ^ v71) & v104)) ^ v38)  + 1200080426u;
    v7   = v104 + Rotl(v6, 12);
    v39  = v38 + a2[6]  + ((((v71 ^ v104) & v7)) ^ v71)   - 1473231341u;
    v40  = v7  + Rotl(v39, 17);
    v72  = v71 + a2[7]  + ((((v104 ^ v7) & v40)) ^ v104)  - 45705983u;
    v73  = v40 + Rotl(v72, 22);
    v105 = v104 + a2[8] + ((((v7 ^ v40) & v73)) ^ v7)     + 1770035416u;
    v106 = v73 + Rotl(v105, 7);
    v8   = v7  + a2[9]  + ((((v40 ^ v73) & v106)) ^ v40)  - 1958414417u;
    v9   = v106 + Rotl(v8, 12);
    v41  = v40 + a2[10] + ((((v73 ^ v106) & v9)) ^ v73)   - 42063u;
    v42  = v9  + Rotl(v41, 17);
    v74  = v73 + a2[11] + ((((v106 ^ v9) & v42)) ^ v106)  - 1990404162u;
    v75  = v42 + Rotl(v74, 22);
    v107 = v106 + a2[12] + ((((v9 ^ v42) & v75)) ^ v9)    + 1804603682u;
    v108 = v75 + Rotl(v107, 7);
    v10  = v9  + a2[13] + ((((v42 ^ v75) & v108)) ^ v42)  - 40341101u;
    v11  = v108 + Rotl(v10, 12);
    v43  = v42 + a2[14] + ((((v75 ^ v108) & v11)) ^ v75)  - 1502002290u;
    v44  = v11 + Rotl(v43, 17);
    v76  = v75 + a2[15] + ((((v108 ^ v11) & v44)) ^ v108) + 1236535329u;
    v77  = v44 + Rotl(v76, 22);

    // --- Round 2 (G) ---
    v109 = v108 + a2[1]  + ((((v44 ^ v77) & v11)) ^ v44)   - 165796510u;
    v110 = v77 + Rotl(v109, 5);
    v12  = v11 + a2[6]  + ((((v77 ^ v110) & v44)) ^ v77)   - 1069501632u;
    v13  = v110 + Rotl(v12, 9);
    v45  = v44 + a2[11] + ((((v110 ^ v13) & v77)) ^ v110)  + 643717713u;
    v46  = v13 + Rotl(v45, 14);
    v78  = v77 + a2[0]  + ((((v13 ^ v46) & v110)) ^ v13)   - 373897302u;
    v79  = v46 + Rotl(v78, 20);
    v111 = v110 + a2[5] + ((((v46 ^ v79) & v13)) ^ v46)    - 701558691u;
    v112 = v79 + Rotl(v111, 5);
    v14  = v13 + a2[10] + ((((v79 ^ v112) & v46)) ^ v79)   + 38016083u;
    v15  = v112 + Rotl(v14, 9);
    v47  = v46 + a2[15] + ((((v112 ^ v15) & v79)) ^ v112)  - 660478335u;
    v48  = v15 + Rotl(v47, 14);
    v80  = v79 + a2[4]  + ((((v15 ^ v48) & v112)) ^ v15)   - 405537848u;
    v81  = v48 + Rotl(v80, 20);
    v113 = v112 + a2[9] + ((((v48 ^ v81) & v15)) ^ v48)    + 568446438u;
    v114 = v81 + Rotl(v113, 5);
    v16  = v15 + a2[14] + ((((v81 ^ v114) & v48)) ^ v81)   - 1019803690u;
    v17  = v114 + Rotl(v16, 9);
    v49  = v48 + a2[3]  + ((((v114 ^ v17) & v81)) ^ v114)  - 187363961u;
    v50  = v17 + Rotl(v49, 14);
    v82  = v81 + a2[8]  + ((((v17 ^ v50) & v114)) ^ v17)   + 1163531501u;
    v83  = v50 + Rotl(v82, 20);
    v115 = v114 + a2[13] + ((((v50 ^ v83) & v17)) ^ v50)   - 1444681467u;
    v116 = v83 + Rotl(v115, 5);
    v18  = v17 + a2[2]  + ((((v83 ^ v116) & v50)) ^ v83)   - 51403784u;
    v19  = v116 + Rotl(v18, 9);
    v51  = v50 + a2[7]  + ((((v116 ^ v19) & v83)) ^ v116)  + 1735328473u;
    v52  = v19 + Rotl(v51, 14);
    v84  = v83 + a2[12] + ((((v19 ^ v52) & v116)) ^ v19)   - 1926607734u;
    v85  = v52 + Rotl(v84, 20);

    // --- Round 3 (H) ---
    v117 = v116 + a2[5]  + (v19 ^ v52 ^ v85)    - 378558u;
    v118 = v85 + Rotl(v117, 4);
    v20  = v19 + a2[8]  + (v52 ^ v85 ^ v118)    - 2022574463u;
    v21  = v118 + Rotl(v20, 11);
    v53  = v52 + a2[11] + (v85 ^ v118 ^ v21)    + 1839030562u;
    v54  = v21 + Rotl(v53, 16);
    v86  = v85 + a2[14] + (v118 ^ v21 ^ v54)    - 35309556u;
    v87  = v54 + Rotl(v86, 23);
    v119 = v118 + a2[1] + (v21 ^ v54 ^ v87)     - 1530992060u;
    v120 = v87 + Rotl(v119, 4);
    v22  = v21 + a2[4]  + (v54 ^ v87 ^ v120)    + 1272893353u;
    v23  = v120 + Rotl(v22, 11);
    v55  = v54 + a2[7]  + (v87 ^ v120 ^ v23)    - 155497632u;
    v56  = v23 + Rotl(v55, 16);
    v88  = v87 + a2[10] + (v120 ^ v23 ^ v56)    - 1094730640u;
    v89  = v56 + Rotl(v88, 23);
    v121 = v120 + a2[13] + (v23 ^ v56 ^ v89)    + 681279174u;
    v122 = v89 + Rotl(v121, 4);
    v24  = v23 + a2[0]  + (v56 ^ v89 ^ v122)    - 358537222u;
    v25  = v122 + Rotl(v24, 11);
    v57  = v56 + a2[3]  + (v89 ^ v122 ^ v25)    - 722521979u;
    v58  = v25 + Rotl(v57, 16);
    v90  = v89 + a2[6]  + (v122 ^ v25 ^ v58)    + 76029189u;
    v91  = v58 + Rotl(v90, 23);
    v123 = v122 + a2[9] + (v25 ^ v58 ^ v91)     - 640364487u;
    v124 = v91 + Rotl(v123, 4);
    v26  = v25 + a2[12] + (v58 ^ v91 ^ v124)    - 421815835u;
    v27  = v124 + Rotl(v26, 11);
    v59  = v58 + a2[15] + (v91 ^ v124 ^ v27)    + 530742520u;
    v60  = v27 + Rotl(v59, 16);
    v92  = v91 + a2[2]  + (v124 ^ v27 ^ v60)    - 995338651u;
    v93  = v60 + Rotl(v92, 23);

    // --- Round 4 (I) ---
    v125 = v124 + a2[0]  + ((~v27 | v93) ^ v60)   - 198630844u;
    v126 = v93 + Rotl(v125, 6);
    v28  = v27 + a2[7]  + ((~v60 | v126) ^ v93)   + 1126891415u;
    v29  = v126 + Rotl(v28, 10);
    v61  = v60 + a2[14] + ((~v93 | v29) ^ v126)   - 1416354905u;
    v62  = v29 + Rotl(v61, 15);
    v94  = v93 + a2[5]  + ((~v126 | v62) ^ v29)   - 57434055u;
    v95  = v62 + Rotl(v94, 21);
    v127 = v126 + a2[12] + ((~v29 | v95) ^ v62)   + 1700485571u;
    v128 = v95 + Rotl(v127, 6);
    v30  = v29 + a2[3]  + ((~v62 | v128) ^ v95)   - 1894986606u;
    v31  = v128 + Rotl(v30, 10);
    v63  = v62 + a2[10] + ((~v95 | v31) ^ v128)   - 1051523u;
    v64  = v31 + Rotl(v63, 15);
    v96  = v95 + a2[1]  + ((~v128 | v64) ^ v31)   - 2054922799u;
    v97  = v64 + Rotl(v96, 21);
    v129 = v128 + a2[8] + ((~v31 | v97) ^ v64)    + 1873313359u;
    v130 = v97 + Rotl(v129, 6);
    v32  = v31 + a2[15] + ((~v64 | v130) ^ v97)   - 30611744u;
    v33  = v130 + Rotl(v32, 10);
    v65  = v64 + a2[6]  + ((~v97 | v33) ^ v130)   - 1560198380u;
    v66  = v33 + Rotl(v65, 15);
    v98  = v97 + a2[13] + ((~v130 | v66) ^ v33)   + 1309151649u;
    v99  = v66 + Rotl(v98, 21);
    v131 = v130 + a2[4] + ((~v33 | v99) ^ v66)    - 145523070u;
    v132 = v99 + Rotl(v131, 6);
    v34  = v33 + a2[11] + ((~v66 | v132) ^ v99)   - 1120210379u;
    v35  = v132 + Rotl(v34, 10);
    v67  = v66 + a2[2]  + ((~v99 | v35) ^ v132)   + 718787259u;
    v68  = v35 + Rotl(v67, 15);
    v100 = v99 + a2[9]  + ((~v132 | v68) ^ v35)   - 343485551u;

    a1[0] += v132;
    a1[1] += v68 + Rotl(v100, 21);
    a1[2] += v68;
    a1[3] += v35;
}

} // namespace

// VIBE_Md5_Init @0x1418c20
void Md5Init(Md5Context* ctx) {
    ctx->state[0] = 1732584193u;     // 0x67452301
    ctx->state[1] = 0xEFCDAB89u;     // -271733879
    ctx->state[2] = 0x98BADCFEu;     // -1732584194
    ctx->state[3] = 271733878u;      // 0x10325476
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}

// VIBE_Md5_Update @0x1418c70
void Md5Update(Md5Context* ctx, const u8* data, u32 len) {
    u32 oldCount = ctx->count[0];
    ctx->count[0] = oldCount + 8u * len;
    if (ctx->count[0] < oldCount)
        ++ctx->count[1];
    ctx->count[1] += len >> 29;

    u32 index = (oldCount >> 3) & 0x3F; // bytes already buffered
    if (index) {
        u8* dst = ctx->buffer + index;
        u32 space = 64 - index;
        if (len < space) {
            std::memmove(dst, data, len);
            return;
        }
        std::memmove(dst, data, space);
        Md5Transform(ctx->state, reinterpret_cast<const u32*>(ctx->buffer));
        data += space;
        len  -= space;
    }
    while (len >= 0x40) {
        std::memmove(ctx->buffer, data, 0x40);
        Md5Transform(ctx->state, reinterpret_cast<const u32*>(ctx->buffer));
        data += 64;
        len  -= 64;
    }
    std::memmove(ctx->buffer, data, len);
}

// VIBE_Md5_Final @0x1418da0
void Md5Final(u8 digest[16], Md5Context* ctx) {
    int index = (ctx->count[0] >> 3) & 0x3F;
    ctx->buffer[index] = 0x80;
    u8* p = ctx->buffer + index + 1;
    u32 pad = 63 - index;
    if (pad >= 8) {
        std::memset(p, 0, pad - 8);
    } else {
        std::memset(p, 0, pad);
        Md5Transform(ctx->state, reinterpret_cast<const u32*>(ctx->buffer));
        std::memset(ctx->buffer, 0, 0x38);
    }
    // Append the 64-bit bit count into the last two words of the block.
    // In the original, a2[20]/a2[21] alias buffer words 14/15.
    reinterpret_cast<u32*>(ctx->buffer)[14] = ctx->count[0];
    reinterpret_cast<u32*>(ctx->buffer)[15] = ctx->count[1];
    Md5Transform(ctx->state, reinterpret_cast<const u32*>(ctx->buffer));
    std::memcpy(digest, ctx->state, 0x10);
    // Original then zeroes the first 4 bytes (state[0]); harmless, kept for fidelity.
    ctx->state[0] = 0;
}

} // namespace guild::compress
