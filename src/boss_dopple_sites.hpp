#pragma once
#include "limits_rules.hpp"
#include <array>
// Retail XMen2.exe bytes the dopple overlay (boss_dopples.cpp) relies on: the shared brain-dispatch site and every
// native service it calls. Checked at startup (all or nothing) and by xml2_xml1_controllers_test against the exe.
namespace boss_dopple_sites {
inline constexpr std::array<limits_rules::guard,30> guards{{
        {0x5173f3,"8b460883f840","brain state dispatch after native gates"},
        {0x517370,"83ec188b44241c535556578b","brain update frame"},
        {0x516930,"51568bf18b46108b88d40300","native lifecycle and wait gate"},
        {0x517a79,"8a86b7020000a8400f848100","native brain postprocessing"},
        {0x45fe60,"8b0d348471008b01ffa06001","game clock"},
        {0x4a1700,"83ec1c568b74242856e82e0c","name lookup"},
        {0x4654b0,"568bf1e8682a06008b16518b","handle resolve"},
        {0x402b60,"568b74240885f6742a8b0657","native ancestry layout"},
        {0x560190,"8a44240484c07405e9f3feff","native random stream"},
        {0x5279c0,"566a018bf1e8c6870300d85c","chance-gated boss taunt (random stream)"},
        {0x5279db,"e83094efff","taunt call"},
        {0x420e10,"83ec0cd905e0b6700056d805","boss taunt"},
        {0x502580,"568bf1808eb502000001e851","native brain wait"},
        {0x529630,"8b41108a886407000081ec84","normal action availability"},
        {0x529690,"8b41108a886407000081ec84","alternate action availability"},
        {0x424a90,"8b44240483ec0c55568bf157","actor move-to"},
        {0x421b90,"83ec0c566800008000680000","movement state reset"},
        {0x423c30,"c781ec03000000000000e951","movement target reset"},
        {0x421da0,"83ec0c568bf1578d86200100","physical stop"},
        {0x423af0,"8b89e80300008b01ff6004","navigation cancel"},
        {0x503810,"568bf18b46108b8878030000","gated immediate transform"},
        {0x50382f,"ff6034","immediate transform tail"},
        {0x503180,"568bf18b46108b8878030000","gated movement transform"},
        {0x50319f,"ffa058010000","movement transform tail"},
        {0x5607b0,"d905300068008b4c2408d901","direction to angles"},
        {0x42c7c0,"53578bf9e8f7bf090084c08b","actor combat target"},
        {0x538290,"8b442404894118c20400","brain target setter"},
        {0x50a250,"6aff688055670064a1000000","target-centred hazard search"},
        {0x4297b0,"568d4c2408e8f6bc03008bf0","handle to physent"},
        {0x497b10,"83ec085355568b74241885f6","physent activation"}}};
}
