#pragma once
#include "xml1_cling_rules.hpp"

namespace xml1_grab_carry_rules
{
// XML1 actor ctor 0x341e0..0x341f0 sets +0x335 & 0x20 for every actor;
// reset 0x2e7b9 preserves it, and no clearer was found among the 29 stores.
// PC removed this field. Emulate it only at flight restriction test points
// XML1 0xda3d5 and 0xda4e5 (after manager vt+0x74, before +0x334 & 1).
// This does not replace PC actteamplayer (+0x3d4 controller index).
// The separate fix7/carry port consumes this seam at those two tests.
constexpr bool xml1_flight_eligible(const void* /*actor*/) noexcept { return true; }
using xml1_cling_rules::setting;
using xml1_cling_rules::parse;
using limits_rules::guard;
constexpr std::uint16_t agreement_revision = 3;
constexpr std::uint32_t feature_mask = 15; // grab1 + pickup/recovery2 + charge collision4 + carry/fix7/upkeep8
// [Game] Xml1FlyingCarry=0 (beta 8 080h): the same rule without the flying carry (its five handlers and the descent
// admission at 0x433e6c); fix7 and the XML1 flight upkeep stay. Bit 16 = fix7/upkeep without the carry, so a peer
// with the carry (mask 15) and one without it (mask 23) refuse each other both ways.
constexpr std::uint32_t feature_mask_no_flying_carry = 1 | 2 | 4 | 16;
constexpr std::uint32_t mask_for(bool flying_carry) noexcept { return flying_carry ? feature_mask : feature_mask_no_flying_carry; }
constexpr std::uint32_t hold_vtable = 0x6932f4, victim_vtable = 0x693360;
constexpr std::uint32_t timeout_va = 0x4edb83;
constexpr std::uint32_t world_hit_gate = 0x42c09e;
inline constexpr std::array<unsigned char,2> world_hit_before{0x74,0x6e},world_hit_after{0x90,0x90};

// XML1 0xdce20 compares the first four bytes case-insensitively. Short names
// cannot match. Do not change these into full-name or substring comparisons.
inline bool retaining_prefix(const char* name)
{
    if (!name) return false;
    char prefix[5]{};
    for (int i = 0; i < 4; ++i)
    {
        if (!name[i]) return false;
        const unsigned char c = static_cast<unsigned char>(name[i]);
        prefix[i] = static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    }
    return std::strcmp(prefix, "grab") == 0 || std::strcmp(prefix, "pick") == 0 ||
           std::strcmp(prefix, "tele") == 0 || std::strcmp(prefix, "abil") == 0;
}

// Native bytes and services are listed below, checked before any patch is made.
inline constexpr std::array<guard, 59> guards{{
    {0x42c093, "85ed7509f683d803000008746e", "PC-only physical/psy gate suppresses thrown-body world impact; XML1 3AF9E forwards it"},
    {0x6932f4, "30d74f00c0d94e0090d84e0010df4e00b0cb480000c54f0040da4e0060c84f00", "hold eight-slot table"},
    {0x693360, "30d74f0040d94e0070d94e0060dd4e0020de4e0000c54f0050ce4f0060c84f00", "shared victim eight-slot table"},
    {0x692f4c, "10d74f00d0bd4f0010eb590010eb5900b0cb480000c54f0050ce4f0060c84f00", "default eight-slot handler"},
    {0x4edb76, "e8e522f7ffd89ee8050000dfe0f6c441755a", "neutral hold deadline call and strict comparison"},
    {0x4edbe2, "5f5e5d8ac35bc20800", "hold update shared return"},
    {0x4ed890, "568b7424088b866c07000085c08944240874", "hold victim cleanup"},
    {0x4ed8be, "e8ed7bf7ff836004fb8bc8e87225f7ff5ec20400", "hold clears victim bit2 and publishes without extra argument"},
    {0x4654b0, "568bf1e8682a06008b16518bcc89118b108bc8ff520c5ec3", "handle resolver through world virtual slot"},
    {0x402b60, "568b74240885f6742a8b06578bceff108b1540b8", "character cast"},
    {0x41d0d0, "8d8160030000c3", "actor style collection"},
    {0x501a50, "8b4424045556578b3833f685ff8be90f84", "named move lookup"},
    {0x501ade, "c208005f8bc65e5dc2", "named lookup PC ret8"},
    {0x432030, "558b6c240885ed568bf10f84de000000", "forced victim transition"},
    {0x43211c, "5b5f5e5dc21800", "forced transition PC ret24"},
    {0x45fe40, "f6416c017418", "publish changed flags"},
    {0x42c340, "56578b7c240c85ff8bf174348b8684030000", "start move"},
    {0x404030, "8d4108c3", "move name key getter vt+d0"},
    {0x69446c, "30404000", "FightMove name slot +d0"},
    {0x602280, "f605487ca30001", "fixed CName intern table getter"},
    {0x6022ce, "b840c4a200c3", "fixed CName intern table base"},
    {0x501a65, "e81608100081e7ffffff008b4cb8048d840108400000", "native CName token mask and string offset"},
    {0x471830, "8b4424048981f0000000", "temporary exclusion"},
    {0x42d550, "83ec2456578b7c243085ff8bf1", "owner relative cleanup"},
    {0x41d020, "538a5c240884db568bf174046a00", "auxiliary state setter"},
    {0x41d060, "8a4424048a91db030000", "auxiliary flag setter"},
    {0x4f5d10, "51538bc8ff5210", "PC slot4 dispatch"},
    {0x4ba57c, "68f0e7680056e8db7f1b0083c40885c0", "large stats parser flag"},
    {0x4ba5a0, "8a9dad02000083c40885c00f94c0c0e00332c32408", "large stored in stats+2ad bit3"},
    {0x693318, "30d74f00f0db4e00e0d84e00e0d84e0000dd4e0000c54f0080d84e0060c84f00", "ally throw handler table"},
    {0x693438, "30d74f00d00d4f0010eb590050074f00b0cb480000c54f0090094f0060c84f00", "pickup grab handler table"},
    {0x69345c, "30d74f00200b4f00c00c4f0050074f00200c4f0000c54f0050ce4f0060c84f00", "pickup handler table"},
    {0x6933a8, "30d74f0080454f0080464f0050464f00c0454f0000c54f00c0494f0060c84f00", "thrown land handler table"},
    {0x429110, "568b74240885f6742a8b06578bceff108b15", "physical entity cast"},
    {0x4edbf0, "83ec3056578d4c2414ff1560", "native ally throw entry"},
    {0x4edc9a, "8b4c24448b51646a006a016a00526aff508bcee87e43f4ff", "ally context timing and forced transition ABI"},
    {0x4edcce, "8b1683c4088d442408508bceff92500100008b16578bceff925c010000", "ally position and reference angle virtual slots"},
    {0x471a38, "8a44240884c07415b8000080bf89863c0200008986400200005ec20800", "PC motion timer field mapping"},
    {0x4f0b68, "6a006a018bcee8ad0ef8ff", "normal pickup motion timer callsite"},
    {0x4f49d2, "d9867c020000d81d30006800dfe0f6c4410f85cf000000", "thrown land health gate"},
    {0x4f49e9, "518b8e78010000", "PC owner block entry"},
    {0x4f4ab8, "5f5e5d32c05bc20800", "recovery false return PC ret8"},
    {0x4f4a3d, "6a018bcee83a32f3ff8bd885db74398b6c2418", "source recovery input continuation"},
    {0x4f4680, "56578b7c240c8b877801000085c08bf1", "PC conditional completion"},
    {0x4f4650, "8b4424048a886407000080e13fc78034040000000000008888640700008b0d8c987200898870070000c2", "source-compatible thrown land cleanup"},
    {0x4f0750, "8b4c2404e8a716f4ffc20400", "PC pickup cancellation throw"},
    {0x4fc020, "8b4c240cb801000000d3e08b4c240885010f95c0", "native attack smash input test"},
    {0x4ee87c, "8b4c241885c9741c6a02e885adfbff8bc8ff15d8f96700dc1d80176900dfe0f6c4057b3f", "PC nonactor contact Z filter"},
    {0x4ee8b3, "85ff74068b178bcfff12", "selected move struck actor check"},
    {0x4ee8bd, "6a01568bcbe879daf3ffa154", "native collision start continuation"},
    {0x4ee8c7, "a154fa6700508bcbe86c4df8ff6a016a008bcbe8f10ef3ff5f5e5bc20c00", "native collision velocity flight exit"},
    {0x42d8e0, "538b5c240856578bf98d4c2410895c2410", "native lock handle by value"},
    {0x42d95b, "899f6c0700005f5e5bc2", "native lock target store and ret4"},
    {0x4de880, "8b0e8b411c8987780100008b7f1c8b1689ba78010000", "native accepted acquisition owner writes"},
    {0x4de896, "8b068b8044040000", "acquisition sound continuation loads"},
    {0x42c270, "568bf18b8e7803000085c974068b0156ff503c", "actor reset dispatches current move exit"},
    {0x42c314, "8a86d803000080e1fc24f7888e64070000c786dc050000000000008886d80300005ec3", "actor reset tail missing source constraint cleanup"},
    {0x42c362, "8a44241084c074078bcee8fffeffff578bcee81715ffff808e64070000", "native move start reset and stage sequence"},
    {0x59eb10, "c20400", "normal victim/default no-op ret4"},
}};
}
