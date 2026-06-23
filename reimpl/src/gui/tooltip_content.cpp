// guild::gui — tooltip content builders, 1:1 from the gilde.exe disassembly.
// See tooltip_content.h for the module contract and the named gaps.
#include "gui/tooltip_content.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// The producer table @0x6496A9 — exact bytes (get_bytes 0x6496A9 x 486; the
// final 3 bytes cover the last row's unaligned int read at +482..+485).
// ---------------------------------------------------------------------------
const u8 kTooltipProductionTable[kProductionRows * kProductionRowStride + 3] = {
    0x00,0x00,0x00,0x29,0x00,0x00,0x00,0x00,0xd4,0x01,0x55,0x01,0x56,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x2a,0x00,0x00,0x00,0x00,0xd4,0x01,0x55,0x01,0x56,0x01,0x58,0x01,0x57,0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x2b,0x00,0x00,0x00,0x00,0xd4,0x01,0x00,0x00,0x55,0x01,0x56,0x01,0x58,0x01,0x57,0x01,0x59,
    0x01,0x5a,0x01,0x32,0xd5,0x01,0x00,0x00,0x00,0x00,0x5b,0x01,0x5c,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x33,0xd5,0x01,0x00,0x00,0x5b,0x01,0x5c,0x01,0x5d,0x01,0x5e,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x34,0xd5,0x01,0x00,0x00,0x5b,0x01,0x5c,0x01,0x5d,0x01,0x5e,0x01,0x60,0x01,0x5f,0x01,0x00,
    0x00,0x00,0x00,0x21,0xd7,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x6d,0x01,0x6e,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x22,0xd7,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x6d,0x01,0x6e,0x01,0x6f,0x01,0x70,0x01,0x00,
    0x00,0x00,0x00,0x23,0xd7,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x6d,0x01,0x6e,0x01,0x6f,0x01,0x70,0x01,0x71,
    0x01,0x72,0x01,0x17,0xd8,0x01,0x00,0x00,0x00,0x00,0x73,0x01,0x74,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x18,0xd8,0x01,0x00,0x00,0x00,0x00,0x73,0x01,0x74,0x01,0x75,0x01,0x77,0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x19,0xd8,0x01,0x00,0x00,0x00,0x00,0x73,0x01,0x74,0x01,0x75,0x01,0x77,0x01,0x78,0x01,0x76,
    0x01,0x00,0x00,0x2f,0xd6,0x01,0x00,0x00,0x00,0x00,0x61,0x01,0x62,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x30,0xd6,0x01,0x00,0x00,0x00,0x00,0x61,0x01,0x62,0x01,0x63,0x01,0x64,0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x31,0xd6,0x01,0x00,0x00,0x00,0x00,0x61,0x01,0x62,0x01,0x63,0x01,0x64,0x01,0x65,0x01,0x66,
    0x01,0x00,0x00,0x35,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xd9,0x01,0x79,0x01,0x7a,0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x36,0x00,0x00,0x00,0x00,0x00,0x00,0xd9,0x01,0x79,0x01,0x7a,0x01,0x7b,0x01,0x7c,0x01,0x00,
    0x00,0x00,0x00,0x37,0x00,0x00,0x00,0x00,0x00,0x00,0xd9,0x01,0x79,0x01,0x7a,0x01,0x7b,0x01,0x7c,0x01,0x7d,
    0x01,0x7e,0x01,0x14,0xda,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x67,0x01,0x68,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x15,0xda,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x67,0x01,0x68,0x01,0x69,0x01,0x6a,0x01,0x00,
    0x00,0x00,0x00,0x16,0xda,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x67,0x01,0x68,0x01,0x69,0x01,0x6a,0x01,0x6b,
    0x01,0x6c,0x01,0x1f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xcd,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,
};

namespace {

inline i16 RdI16(const u8* p) {
    return static_cast<i16>(static_cast<u16>(p[0] | (p[1] << 8)));
}

// The shared clamp block (0x4f7aa5 / 0x4f81c2) applied through the host edges.
void ClampFormToScreen(int form, int screenW, TooltipContentHost& host) {
    int x = 0, y = 0, w = 0;
    if (!host.WindowGeom(form, &x, &y, &w))
        return;
    int nx = 0;
    if (Tooltip_ClampToScreen(x, w, screenW, &nx))
        host.MoveWindow(nx, y);   // VIBE_Widget_LayoutBounds(newX, y, widget)
}

} // namespace

// ---------------------------------------------------------------------------
bool Tooltip_ClampToScreen(int winX, int winW, int screenW, int* clampedX) {
    // if ( (w>>16) + (x>>16) > (dword_69FFBC>>16) - 16 )
    //     Widget_LayoutBounds(HIWORD(dword_69FFBC) - 16 - w, y, widget);
    if (winW + winX > screenW - kTooltipScreenMargin) {
        if (clampedX) *clampedX = screenW - kTooltipScreenMargin - winW;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4f7e32..0x4f80df — the weapon/tool "used by" code mapping.
// ---------------------------------------------------------------------------
int Tooltip_WeaponUserCodes(int objectId, u8 out[2]) {
    // 0x4f7e32: 449/450/451 -> the {24,25} pair.
    if (objectId == 449 || objectId == 450 || objectId == 451) {
        out[0] = 24; out[1] = 25;                            // 0x4f7e46/0x4f7e48
        return 2;
    }
    // 0x4f8002..0x4f8033: 452/453/454 -> {30}; 455 falls through to the ranges.
    if (objectId == 452 || objectId == 453 || objectId == 454) {
        out[0] = 30;                                         // 0x4f8016
        return 1;
    }
    // 0x4f8035..0x4f8068: 464..466 / 445..448 -> {32}.
    if ((objectId >= 464 && objectId <= 466) ||
        (objectId >= 445 && objectId <= 448)) {
        out[0] = 32;                                         // 0x4f804b
        return 1;
    }
    // 0x4f806a..0x4f809d: 439..441 / 458..460 -> {30}.
    if ((objectId >= 439 && objectId <= 441) ||
        (objectId >= 458 && objectId <= 460)) {
        out[0] = 30;                                         // 0x4f8080
        return 1;
    }
    // 0x4f809f..0x4f80dd: 461..463 / 442 / 443..444 -> {31}.
    if ((objectId >= 461 && objectId <= 463) || objectId == 442 ||
        (objectId >= 443 && objectId <= 444)) {
        out[0] = 31;                                         // 0x4f80b5
        return 1;
    }
    return 0;
}

// gilde.exe 0x4f7ebc..0x4f7ee5 — the "+market" row gate.
bool Tooltip_ObjectMarketRow(int objectId, u8 classByte) {
    const bool weaponSet = objectId >= 449 && objectId <= 455; // {1C1..1C7}
    if (weaponSet && objectId != 455)
        return false;                       // 0x4f7ecd: != 1C7 -> skip
    return classByte == 23 || classByte == 37; // 0x4f7ee2/0x4f8125
}

// gilde.exe 0x4f7c27..0x4f7c75 — the owner value-ratio line.
int Tooltip_ObjectValueRatio(i32 baseValue, double marketValue, int workerByte) {
    // v40 = (double)workerByte * dbl_6206D8 * dbl_6206E0 + marketValue;
    // v15 = (double)baseValue / v40;  fistp -> int.
    // 0x4f7c59: fild WORD ptr (signed 16-bit) on the active-worker byte.
    const double denom =
        static_cast<double>(static_cast<i16>(workerByte)) *
            TooltipObjWorkerWeight() * kObjWorkerScale +
        marketValue;
    // 0x4f7c4a/0x4f7c55/0x4f7c5d: var_54 is a QWORD whose high dword is zeroed
    // (esi=0) and the value is loaded with `fild qword` — i.e. baseValue is
    // taken UNSIGNED (32-bit zero-extended into 64-bit) before the divide.
    return static_cast<int>(
        static_cast<double>(static_cast<u32>(baseValue)) / denom);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4f7a10 — VIBE_Tooltip_BuildObject.
// ---------------------------------------------------------------------------
int Tooltip_BuildObjectContent(i16 code, const TooltipObjectView& rec,
                               const TooltipObjectEnv& env,
                               TooltipContentHost& host) {
    // 0x4f7a35..0x4f7a5b: Avatar_LookupById -> Waffen vs Handelsgut form.
    const char* form = env.isWeapon ? kFormTooltipWeapon : kFormTooltipTradegood;
    const int handle = host.LoadForm(form);          // 0x4f7a69
    host.CenterChildWindows(handle);                 // 0x4f7a75
    ClampFormToScreen(handle, env.screenW, host);    // 0x4f7aa5..0x4f7ae7

    // Window 2: header icon + description text.
    host.SelectWindow(0, 2);                         // 0x4f7afa
    host.AddIconObject(code + kObjIconBias);         // 0x4f7b12 (code+206)
    host.Text(2 * code + kObjDescTextBias);          // 0x4f7b26 (2*code+2152)

    // Window 1: the object name.
    host.SelectWindow(0, 1);                         // 0x4f7b36
    host.Text(2 * code + kObjNameTextBias);          // 0x4f7b3c (2*code+2151)

    // Window 3: durability OR the market-price pair.
    host.SelectWindow(0, 3);                         // 0x4f7b4b
    if (rec.durability) {
        host.TextArg(kTipDurability, rec.durability);          // 0x4f7b70
    } else {
        host.TextArg2(kTipPricePair, env.priceCached, env.priceNow); // 0x4f7f9e
    }

    // Window 4 (selected with the REAL form handle, 0x4f7b91): the value line.
    host.SelectWindow(handle, 4);                    // 0x4f7b91
    if (env.hasOwner) {
        // 0x4f7ba7..0x4f7c80: workstation sum + person collect + market value
        // fold into env; the ratio math is byte-exact.
        host.TextArg(kTipValueRatio,
                     Tooltip_ObjectValueRatio(rec.baseValue, env.marketValue,
                                              env.activeWorkerByte)); // 0x4f7c80
    } else {
        host.TextArg(kTipBaseValue, rec.baseValue);  // 0x4f7fd1
    }

    // Window 4: the ingredients header + up to four (count,item) rows.
    host.SelectWindow(0, 4);                         // 0x4f7c8f
    host.Text(kTipIngredients);                      // 0x4f7c96 (0x25)
    {
        int y = kTipRowBaseY;                        // mov edi, 28h
        for (int k = 0; k < 4; ++k) {                // esi = rec .. rec+8 step 2
            const u16 item = rec.ingredientItem[k];  // word +46+2k
            const u16 cnt  = rec.ingredientCount[k]; // word +38+2k
            if (!item)
                continue;
            host.AddAnimatedObject(static_cast<i16>(item) + kObjIconBias,
                                   kTipRowSlotIn, y);            // 0x4f7cf4
            host.TextFmt(kFmtCountName, cnt,
                         2 * static_cast<i16>(item) + kObjNameTextBias); // 0x4f7d07
            y += kTipRowPitch;                       // add edi, 16h
        }
    }

    // Window 3: the producers header + the @0x6496A9 table walk.
    host.SelectWindow(0, 3);                         // 0x4f7d33
    host.Text(kTipProducers);                        // 0x4f7d41 (0x26)
    int rows = 0;                                    // var_24 reset (0x4f7d4b)
    for (int r = 0; r < kProductionRows; ++r) {      // cmp esi, 17h
        const u8* row = kTooltipProductionTable + kProductionRowStride * r;
        for (int off = 0; off != 20; off += 2) {     // v22 = 21r .. 21r+18
            // (int at row+off+2) >> 16 == code, and the id-461 suppression.
            if (RdI16(row + off + 4) != code || code == 461)
                continue;                            // 0x4f7d8a / 0x4f7d8e
            const int bcode = static_cast<signed char>(row[3]); // sar 24 of +0
            host.AddAnimatedObject(bcode + kBldIconBias, kTipRowSlotBy,
                                   kTipRowPitch * rows + kTipRowBaseY); // 0x4f7db9
            host.TextFmt(kFmtName,
                         kBldNameStride * bcode + kBldNameBias, 0);     // 0x4f7dd5
            ++rows;
        }
    }

    // The weapon/tool "used by" rows (class 23/37 only).
    if (rec.classByte == 23 || rec.classByte == 37) { // 0x4f7e29/0x4f7fde
        u8 codes[2] = {0, 0};
        const int n = Tooltip_WeaponUserCodes(code, codes);
        for (int i = 0; i < n; ++i) {                 // 0x4f7e63 loop
            host.AddAnimatedObject(codes[i] + kBldIconBias, kTipRowSlotBy,
                                   kTipRowPitch * rows + kTipRowBaseY);
            host.TextFmt(kFmtName,
                         kBldNameStride * codes[i] + kBldNameBias, 0);
            ++rows;
        }
    }

    // The "+market" row (0x4f7ebc gate, 0x4f7eeb..0x4f7f29 emission).
    if (Tooltip_ObjectMarketRow(code, rec.classByte)) {
        host.AddAnimatedObject(kMarketIconObj, kTipRowSlotBy,
                               kTipRowPitch * (rows + 2) - 4);  // 22*(n+2)-4
        host.TextFmt(kFmtName, kMarketTextId, 0);               // 0x4f7f21
        ++rows;
    }

    // No window-3 rows at all -> the "$C" clear token (0x4f7f30/0x4f8133).
    if (!rows)
        host.TextFmt(kFmtClear, 0, 0);

    return handle;                                   // 0x4f7f42
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4f8154 — VIBE_Tooltip_BuildUpgrade.
// ---------------------------------------------------------------------------
int Tooltip_BuildUpgradeContent(i16 code, u8 objClassByte,
                                const TooltipObjectView& rec,
                                const TooltipUpgradeOwnerView& owner,
                                const TooltipUpgradeEnv& env,
                                TooltipContentHost& host) {
    // 0x4f816b: class 29 -> -1 (the Tooltip_UpgradeApplies gate, REUSED).
    if (!Tooltip_UpgradeApplies(&objClassByte, 0))
        return -1;

    const int handle = host.LoadForm(kFormTooltipUpgrade);  // 0x4f8189
    host.CenterChildWindows(handle);                        // 0x4f8194
    ClampFormToScreen(handle, env.screenW, host);           // 0x4f81c2..0x4f8204

    host.SelectWindow(0, 2);                                // 0x4f8210
    host.AddIconObject(code + kObjIconBias);                // 0x4f8228
    host.Text(2 * code + kObjDescTextBias);                 // 0x4f823c

    host.SelectWindow(0, 1);                                // 0x4f824c
    host.Text(2 * code + kObjNameTextBias);                 // 0x4f8252

    host.SelectWindow(0, 3);                                // 0x4f8270
    if (rec.durability)
        host.TextArg(kTipDurability, rec.durability);       // 0x4f8287
    else
        host.TextArg(kTipUpgradePrice, env.scaledPrice);    // 0x4f83a6/0x4f8287
    host.TextArg(kTipBaseValue, rec.baseValue);             // 0x4f8295

    // 0x4f829a..0x4f82c3: no owner, or object class 2/6 -> done.
    if (!owner.present)
        return handle;
    if (owner.objClassGate == 2 || owner.objClassGate == 6)
        return handle;

    // 0x4f82f8..0x4f83cf: the 64-slot scan over the owner's 589-byte row.
    int value = 0;                                          // edi = 0
    u8  kind  = 0;                                          // var_10 = 0
    for (int i = 0; i < 64; ++i) {
        if ((owner.slotWords[i] & 0x7FFF) == static_cast<u16>(code)) {
            value = owner.valueBytes[i];                    // +483+i (0x4f8314)
            kind  = owner.kindBytes[i];                     // +419+i (0x4f830d)
            break;
        }
    }
    host.Text(kTipUpgradeHead);                             // 0x4f8320 (0x24)
    if (kind) {
        const int k = static_cast<signed char>(kind);       // sar 24 (0x4f8336)
        if (k != -1 && k != 255) {
            const int textId = 2 * k + kUpgEffectBias;      // 0x4f8347
            if (kind == 3)
                host.TextFmt(kFmtPlusAmount, value, textId);   // 0x4f835c
            else
                host.TextFmt(kFmtPercentName, value, textId);  // 0x4f83e1
        }
    }
    return handle;                                          // 0x4f8364
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4f78e4 — VIBE_Tooltip_BuildBuilding (emission order; the values
// are Tooltip_BuildingLayout's, kBuildingColors recovered in tooltip_build).
// ---------------------------------------------------------------------------
int Tooltip_BuildBuildingContent(int code, u8 colorSelector, i32 extraField,
                                 i32 salePrice, TooltipContentHost& host) {
    const u32 color =
        kBuildingColors[colorSelector % kBuildingColorCount]; // v10[rec[583]]
    const int handle = host.LoadForm(kFormTooltipBuilding);   // 0x4f7924
    host.CenterChildWindows(handle);                          // 0x4f7930
    host.SelectWindow(0, 1);                                  // 0x4f7937
    host.TextArg2(kTipBldTitle, kBldNameStride * code + kBldNameBias,
                  static_cast<int>(color));                   // 0x4f7967 (0x27)
    host.SelectWindow(0, 2);                                  // 0x4f7977
    // 0x4f7986 ORIGINAL QUIRK: VIBE_Object_AddToWindow(ecx=win, dx=0, ax=0, ebx)
    // — the object id is passed in EBX, but the building path never loads it
    // (no `lea ebx,[code+0x3F2]` like the object path at 0x4f7b0c).  EBX still
    // holds the form-name string pointer set at 0x4f790c (`mov ebx, offset
    // aTooltipTooltip_0`), preserved across the callee-saved RenderRichString/
    // CenterChildWindows calls.  So the original's header icon id is a non-
    // reproducible .rdata pointer (~0x620660), i.e. a latent bug.  We cannot
    // carry that pointer through the integer AddIconObject edge; code+1010 is the
    // intended-icon placeholder (NOT what the binary actually passes).
    host.AddIconObject(code + kBldIconBias);                  // 0x4f7986 (see above)
    host.Text(kBldNameStride * code + kBldNameBias + 1);      // 0x4f7995 (14c+1079)
    host.SelectWindow(0, 3);                                  // 0x4f79a4
    host.TextArg(kTipBldDesc, static_cast<int>(color));       // 0x4f79bd (0x2A)
    host.TextArg(kTipBldPrice, salePrice);                    // 0x4f79ea (0x28)
    host.TextArg(kTipBldExtra, extraField);                   // 0x4f79fb (0x29)
    return handle;                                            // 0x4f7a03
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4f84ac — VIBE_Tooltip_BuildPerson.
// ---------------------------------------------------------------------------
int Tooltip_BuildPersonContent(const TooltipPersonView& p,
                               const TooltipPersonEnv& env,
                               TooltipContentHost& host) {
    const int handle = host.LoadForm(kFormTooltipPerson);   // 0x4f84c5
    host.CenterChildWindows(handle);                        // 0x4f84d2

    host.SelectWindow(0, 1);                                // 0x4f84d9
    host.TextFmt(kFmtPersonHead, p.id, 0);                  // 0x4f84e9

    host.SelectWindow(0, 4);                                // 0x4f84f8
    if (env.statusCard)                                     // ResolveStatusFlags
        host.PersonCard(kPersonCardX, kPersonCardY);        // 0x4f8773 (166, 5)

    host.SelectWindow(0, 2);                                // 0x4f8517
    host.TextArg(kTipPersonCash, env.cash);                 // 0x4f8534 (0x2B)

    // 0x4f854a..0x4f8568: job line — base 294 (370 female) + rank.
    // 0x4f8560: `sar edx, 18h` — the job code is a SIGNED byte.
    const int jobBase = p.female ? kPersonJobBaseF : kPersonJobBaseM;
    host.TextArg2(kTipPersonJob,
                  static_cast<signed char>(p.jobCode) + jobBase, env.rank); // 0x4f8568

    host.Text(kTipPersonTraitsHead);                        // 0x4f8572 (0x2D)
    if (p.trait1 || p.trait2) {                             // 0x4f8580/0x4f8787
        const int traitBase = p.female ? kPersonTraitBaseF : kPersonTraitBaseM;
        if (p.trait1)
            host.TextFmt(kFmtIndent, p.trait1 + traitBase, 0);   // 0x4f85b0
        if (p.trait2)
            host.TextFmt(kFmtIndent, p.trait2 + traitBase, 0);   // 0x4f85e0
    } else {
        host.TextFmt(kFmtIndent, kPersonTraitBaseM, 0);     // 0x4f8794 (525)
    }

    // 0x4f85e8..0x4f8601: religion — base 272 (279 female).
    const int relBase = p.female ? kPersonReligionBaseF : kPersonReligionBaseM;
    host.TextArg(kTipPersonReligion, p.religion + relBase); // 0x4f8601 (0x37)

    host.TextArg(kTipPersonWealth, env.wealth);             // 0x4f8616 (0x2E)
    host.Text(kTipPersonFamilyHead);                        // 0x4f8620 (0x2F)

    // 0x4f862b..0x4f8811: spouse / betrothed / unmarried.
    if (env.spouseNameId >= 0) {
        host.TextArg(kTipPersonSpouse, env.spouseNameId);       // 0x4f864d
    } else if (env.betrothedNameId >= 0) {
        host.TextArg(kTipPersonBetrothed, env.betrothedNameId); // 0x4f87f5
    } else {
        host.Text(kTipPersonUnmarried);                         // 0x4f8807
    }

    // 0x4f8655: class line — byte +12 + 1070.  0x4f8658: `sar eax, 18h` (signed).
    host.TextArg(kTipPersonClass,
                 static_cast<signed char>(p.classCode) + kPersonClassBase); // 0x4f8663

    host.Text(kTipPersonChildHead);                         // 0x4f8674 (0x34)
    bool anyChild = false;                                  // ecx (0x4f866f)
    for (int i = 0; i < 5; ++i) {                           // edx = esi..esi+14h
        if (env.childNameIds[i] < 0)
            continue;
        host.TextFmt(kFmtIndentName, env.childNameIds[i], 0);  // 0x4f869b
        anyChild = true;
    }
    if (!anyChild)
        host.TextFmt(kFmtIndent, kPersonNoChildText, 0);    // 0x4f881d (53)

    // Window 3: the five skill label+bar rows (0x4f86be..0x4f8742).
    host.SelectWindow(0, 3);                                // 0x4f86be
    for (int i = 0; i < kPersonSkillRows; ++i) {
        host.TextLabel(0, kPersonSkillPitch * i,
                       kPersonSkillTextBase + i);           // 0x4f8706
        host.SkillBar(100, 2 + kPersonSkillPitch * i, i,
                      kPersonSkillBarGfx);                  // 0x4f871f
    }
    // VIBE_Interaction_DispatchPanelEvent(7, person, 0, 0) @0x4f874f — the
    // panel-event hook is the interaction cluster's edge (no content output).
    return handle;                                          // 0x4f8754
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4f83e8 — VIBE_Tooltip_BuildContact (found branch).
// ---------------------------------------------------------------------------
int Tooltip_BuildContactContent(int textIndex, TooltipContentHost& host) {
    const int handle = host.LoadForm(kFormTooltipContact);  // 0x4f8461
    host.CenterChildWindows(handle);                        // 0x4f846d
    host.SelectWindow(0, 1);                                // 0x4f8474
    host.TextFmt(kFmtContactHead, textIndex, 0);            // 0x4f8484
    host.SelectWindow(0, 2);                                // 0x4f848f
    host.Text(textIndex + 1);                               // 0x4f8495
    return handle;                                          // 0x4f849f
}

} // namespace guild::gui
