// Flexible VCA pure integer mappings. Two 129-knot log/exp tables (1032 bytes).
// Knots are uniform on the normalized mantissa, NOT the input amplitude: even
// the first sample of an 8-second concave ramp retains useful shape resolution.
// No runtime float, table construction, allocation or variable division here.
#pragma once
#include "unit.h"

namespace seance::vca_envelope {
inline uint32_t unipolar(q15 v) { return v < 0 ? 0u : (uint32_t)v; }
inline q15 bipolar(q15 v) { return v == -32768 ? (q15)-32767 : v; }
// round(log2(1 + i/128) * 2^20)
inline constexpr uint32_t LOG_Q20[129] = {
    0u, 11773u, 23454u, 35047u, 46551u, 57968u, 69300u, 80547u,
    91711u, 102794u, 113796u, 124719u, 135563u, 146330u, 157021u, 167637u,
    178179u, 188648u, 199045u, 209372u, 219628u, 229815u, 239934u, 249985u,
    259971u, 269891u, 279746u, 289537u, 299266u, 308932u, 318537u, 328082u,
    337566u, 346991u, 356359u, 365668u, 374920u, 384117u, 393257u, 402343u,
    411375u, 420353u, 429278u, 438150u, 446971u, 455741u, 464460u, 473129u,
    481749u, 490320u, 498843u, 507318u, 515745u, 524126u, 532461u, 540750u,
    548995u, 557194u, 565349u, 573460u, 581529u, 589554u, 597537u, 605478u,
    613378u, 621236u, 629054u, 636832u, 644570u, 652269u, 659928u, 667549u,
    675132u, 682677u, 690185u, 697655u, 705089u, 712487u, 719848u, 727174u,
    734464u, 741720u, 748941u, 756127u, 763280u, 770399u, 777484u, 784537u,
    791557u, 798544u, 805500u, 812423u, 819315u, 826176u, 833005u, 839804u,
    846573u, 853311u, 860020u, 866699u, 873348u, 879969u, 886561u, 893124u,
    899658u, 906165u, 912644u, 919095u, 925518u, 931915u, 938284u, 944627u,
    950944u, 957234u, 963498u, 969736u, 975949u, 982136u, 988298u, 994435u,
    1000547u, 1006635u, 1012698u, 1018737u, 1024752u, 1030743u, 1036711u, 1042655u,
    1048576u,
};
// round(2^(-i/128) * 2^31)
inline constexpr uint32_t EXP_NEG_Q31[129] = {
    2147483648u, 2135885998u, 2124350982u, 2112878262u, 2101467502u, 2090118366u, 2078830522u, 2067603638u,
    2056437387u, 2045331439u, 2034285470u, 2023299156u, 2012372174u, 2001504204u, 1990694927u, 1979944027u,
    1969251188u, 1958616096u, 1948038440u, 1937517909u, 1927054196u, 1916646992u, 1906295993u, 1896000896u,
    1885761398u, 1875577199u, 1865448001u, 1855373507u, 1845353420u, 1835387448u, 1825475297u, 1815616678u,
    1805811301u, 1796058879u, 1786359126u, 1776711757u, 1767116489u, 1757573041u, 1748081133u, 1738640488u,
    1729250827u, 1719911875u, 1710623359u, 1701385007u, 1692196547u, 1683057710u, 1673968228u, 1664927835u,
    1655936265u, 1646993254u, 1638098541u, 1629251865u, 1620452965u, 1611701585u, 1602997467u, 1594340357u,
    1585730000u, 1577166143u, 1568648537u, 1560176931u, 1551751076u, 1543370725u, 1535035634u, 1526745556u,
    1518500250u, 1510299473u, 1502142985u, 1494030547u, 1485961921u, 1477936870u, 1469955159u, 1462016553u,
    1454120821u, 1446267730u, 1438457051u, 1430688553u, 1422962010u, 1415277195u, 1407633882u, 1400031848u,
    1392470869u, 1384950723u, 1377471191u, 1370032052u, 1362633090u, 1355274085u, 1347954824u, 1340675091u,
    1333434672u, 1326233356u, 1319070932u, 1311947188u, 1304861917u, 1297814910u, 1290805962u, 1283834865u,
    1276901417u, 1270005413u, 1263146652u, 1256324931u, 1249540052u, 1242791816u, 1236080024u, 1229404479u,
    1222764986u, 1216161350u, 1209593378u, 1203060876u, 1196563654u, 1190101520u, 1183674286u, 1177281762u,
    1170923762u, 1164600099u, 1158310587u, 1152055042u, 1145833280u, 1139645120u, 1133490379u, 1127368878u,
    1121280436u, 1115224875u, 1109202018u, 1103211687u, 1097253708u, 1091327906u, 1085434106u, 1079572136u,
    1073741824u,
};

// 2^(-x), x unsigned Q20, result unsigned Q31 including exact unity.
inline uint32_t exp_negative(uint32_t x) {
    const uint32_t whole = x >> 20;
    if (whole > 31) return 0;
    const uint32_t index = (x >> 13) & 127u, f = x & 8191u;
    const uint32_t a = EXP_NEG_Q31[index], delta = a - EXP_NEG_Q31[index + 1];
    return (a - (uint32_t)(((uint64_t)delta * f + 4096u) >> 13)) >> whole;
}
inline uint32_t exponent_q20(q15 bend) {
    const int32_t b = bipolar(bend);
    // 2^21 = 32768*64: this exact decomposition keeps the signed numerator
    // in 32 bits, so ARM uses a constant reciprocal instead of __aeabi_ldivmod.
    const int32_t scaled = b * 64;
    const uint32_t distance = (uint32_t)(2097152 - scaled - scaled / 32767);
    return (exp_negative(distance) + 256u) >> 9; // 4 * Q31 -> Q20
}
// p is Q32 [0, 2^32]. Exponent is precomputed at stage entry for audio.
inline q15 shape(uint64_t p, uint32_t exponent) {
    if (!p) return 0;
    if (p >= (1ull << 32)) return 32767;
    if (exponent == 1048576u) return (q15)((p * 32767u + (1ull << 31)) >> 32);
    const uint32_t x = (uint32_t)p;
    const unsigned shift = (unsigned)__builtin_clz(x);
    const uint32_t mantissa = x << shift;
    const uint32_t index = (mantissa >> 24) & 127u, f = mantissa & 0xffffffu;
    const uint32_t log = LOG_Q20[index] + (uint32_t)(
        ((uint64_t)(LOG_Q20[index + 1] - LOG_Q20[index]) * f + 0x800000u) >> 24);
    const uint32_t negative_log = ((shift + 1u) << 20) - log;
    const uint32_t product = (uint32_t)(((uint64_t)negative_log * exponent + 524288u) >> 20);
    return (q15)(((uint64_t)exp_negative(product) * 32767u + (1ull << 30)) >> 31);
}
inline q15 curve_phase(uint64_t progress_q32, q15 bend) {
    return shape(progress_q32, exponent_q20(bend));
}
inline q15 curve(q15 progress, q15 bend) {
    const uint32_t p = unipolar(progress);
    if (!p || p == 32767u || !bend) return (q15)p;
    return curve_phase(((uint64_t)p << 32) / 32767u, bend);
}
// Exact unsigned endpoint multiplication with round-to-nearest, constant divisor.
inline q15 multiply(q15 a, q15 b) {
    return (q15)((unipolar(a) * unipolar(b) + 16383u) / 32767u);
}
inline q15 opening(q15 env, q15 floor, bool duck) {
    const q15 f = (q15)unipolar(floor), e = (q15)unipolar(env);
    return (q15)(f + multiply((q15)(32767 - f), duck ? (q15)(32767 - e) : e));
}
inline uint32_t time_frames(q15 value, bool hold = false) {
    uint32_t raw = unipolar(value);
    if (hold && !raw) return 0;
    // round(log2(8000) * 2^20) = 13595610. Positive HOLD reserves raw zero.
    const uint32_t distance = hold
        ? (uint32_t)((uint64_t)(32767u - raw) * 13595610u / 32766u)
        : (uint32_t)((uint64_t)(32767u - raw) * 13595610u / 32767u);
    // Authoring uses integer milliseconds, exactly reflected by DSP sample counts.
    // Quantize once here (never in the phase ramp) so e.g. the 780ms hero is exact.
    return 32u * (uint32_t)(((uint64_t)8000u * exp_negative(distance) + (1ull << 30)) >> 31);
}
// UI/fixture convenience only: bounded 15-step inverse, nearest represented time.
inline q15 time_value(uint32_t frames, bool hold = false) {
    if (hold && !frames) return 0;
    if (frames <= 32u) return hold ? 1 : 0;
    if (frames >= 256000u) return 32767;
    uint32_t lo = hold ? 1u : 0u, hi = 32767u;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) >> 1;
        if (time_frames((q15)mid, hold) < frames) lo = mid + 1u;
        else hi = mid;
    }
    const uint32_t a = time_frames((q15)(lo - 1u), hold), b = time_frames((q15)lo, hold);
    return (q15)(frames - a <= b - frames ? lo - 1u : lo);
}
} // namespace seance::vca_envelope
