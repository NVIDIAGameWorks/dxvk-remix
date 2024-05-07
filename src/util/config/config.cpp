/*
* Copyright (c) 2019-2023, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include <array>
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <utility>
#include <filesystem>

#include "config.h"

#include "../log/log.h"

#include "../util_env.h"

namespace dxvk {

  const static std::vector<std::pair<const char*, Config>> g_appDefaults = {{
    /* Assassin's Creed Syndicate: amdags issues  */
    { R"(\\ACS\.exe$)", {{
      { "dxgi.customVendorId",              "10de" },
    }} },
    /* Dissidia Final Fantasy NT Free Edition */
    { R"(\\dffnt\.exe$)", {{
      { "dxgi.deferSurfaceCreation",        "True" },
    }} },
    /* Elite Dangerous: Compiles weird shaders    *
     * when running on AMD hardware               */
    { R"(\\EliteDangerous64\.exe$)", {{
      { "dxgi.customVendorId",              "10de" },
    }} },
    /* The Vanishing of Ethan Carter Redux        */
    { R"(\\EthanCarter-Win64-Shipping\.exe$)", {{
      { "dxgi.customVendorId",              "10de" },
    }} },
    /* The Evil Within: Submits command lists     * 
     * multiple times                             */
    { R"(\\EvilWithin(Demo)?\.exe$)", {{
      { "d3d11.dcSingleUseMode",            "False" },
    }} },
    /* Far Cry 3: Assumes clear(0.5) on an UNORM  *
     * format to result in 128 on AMD and 127 on  *
     * Nvidia. We assume that the Vulkan drivers  *
     * match the clear behaviour of D3D11.        */
    { R"(\\(farcry3|fc3_blooddragon)_d3d11\.exe$)", {{
      { "dxgi.nvapiHack",                   "False" },
    }} },
    /* Far Cry 4: Same as Far Cry 3               */
    { R"(\\FarCry4\.exe$)", {{
      { "dxgi.nvapiHack",                   "False" },
    }} },
    /* Frostpunk: Renders one frame with D3D9     *
     * after creating the DXGI swap chain         */
    { R"(\\Frostpunk\.exe$)", {{
      { "dxgi.deferSurfaceCreation",        "True" },
    }} },
    /* Nioh: See Frostpunk, apparently?           */
    { R"(\\nioh\.exe$)", {{
      { "dxgi.deferSurfaceCreation",        "True" },
    }} },
    /* Quantum Break: Mever initializes shared    *
     * memory in one of its compute shaders       */
    { R"(\\QuantumBreak\.exe$)", {{
      { "d3d11.zeroInitWorkgroupMemory",    "True" },
    }} },
    /* Anno 2205: Random crashes with state cache */
    { R"(\\anno2205\.exe$)", {{
      { "dxvk.enableStateCache",            "False" },
    }} },
    /* Fifa '19+: Binds typed buffer SRV to shader *
     * that expects raw/structured buffer SRV     */
    { R"(\\FIFA(19|[2-9][0-9])(_demo)?\.exe$)", {{
      { "dxvk.useRawSsbo",                  "True" },
    }} },
    /* Resident Evil 2/3: Ignore WaW hazards      */
    { R"(\\re(2|3|3demo)\.exe$)", {{
      { "d3d11.relaxedBarriers",            "True" },
    }} },
    /* Devil May Cry 5                            */
    { R"(\\DevilMayCry5\.exe$)", {{
      { "d3d11.relaxedBarriers",            "True" },
    }} },
    /* Call of Duty WW2                           */
    { R"(\\s2_sp64_ship\.exe$)", {{
      { "dxgi.nvapiHack",                   "False" },
    }} },
    /* Need for Speed 2015                        */
    { R"(\\NFS16\.exe$)", {{
      { "dxgi.nvapiHack",                   "False" },
    }} },
    /* Mass Effect Andromeda                      */
    { R"(\\MassEffectAndromeda\.exe$)", {{
      { "dxgi.nvapiHack",                   "False" },
    }} },
    /* Mirror`s Edge Catalyst: Crashes on AMD     */
    { R"(\\MirrorsEdgeCatalyst(Trial)?\.exe$)", {{
      { "dxgi.customVendorId",              "10de" },
    }} },
    /* Star Wars Battlefront (2015)               */
    { R"(\\starwarsbattlefront(trial)?\.exe$)", {{
      { "dxgi.nvapiHack",                   "False" },
    }} },
    /* Dark Souls Remastered                      */
    { R"(\\DarkSoulsRemastered\.exe$)", {{
      { "d3d11.constantBufferRangeCheck",   "True" },
    }} },
    /* Grim Dawn                                  */
    { R"(\\Grim Dawn\.exe$)", {{
      { "d3d11.constantBufferRangeCheck",   "True" },
    }} },
    /* NieR:Automata                              */
    { R"(\\NieRAutomata\.exe$)", {{
      { "d3d11.constantBufferRangeCheck",   "True" },
    }} },
    /* NieR Replicant                             */
    { R"(\\NieR Replicant ver\.1\.22474487139\.exe)", {{
      { "dxgi.syncInterval",                "1"   },
      { "dxgi.maxFrameRate",                "60"  },
    }} },
    /* SteamVR performance test                   */
    { R"(\\vr\.exe$)", {{
      { "d3d11.dcSingleUseMode",            "False" },
    }} },
    /* Hitman 2 and 3 - requires AGS library      */
    { R"(\\HITMAN(2|3)\.exe$)", {{
      { "dxgi.customVendorId",              "10de" },
    }} },
    /* Modern Warfare Remastered                  */
    { R"(\\h1_[ms]p64_ship\.exe$)", {{
      { "dxgi.customVendorId",              "10de" },
    }} },
    /* Titan Quest                                */
    { R"(\\TQ\.exe$)", {{
      { "d3d11.constantBufferRangeCheck",   "True" },
    }} },
    /* Saints Row IV                              */
    { R"(\\SaintsRowIV\.exe$)", {{
      { "d3d11.constantBufferRangeCheck",   "True" },
    }} },
    /* Saints Row: The Third                      */
    { R"(\\SaintsRowTheThird_DX11\.exe$)", {{
      { "d3d11.constantBufferRangeCheck",   "True" },
    }} },
    /* Crysis 3 - slower if it notices AMD card     *
     * Apitrace mode helps massively in cpu bound   *
     * game parts                                   */
    { R"(\\Crysis3\.exe$)", {{
      { "dxgi.customVendorId",              "10de" },
      { "d3d11.apitraceMode",               "True" },
    }} },
    /* Crysis 3 Remastered                          *
     * Apitrace mode helps massively in cpu bound   *
     * game parts                                   */
    { R"(\\Crysis3Remastered\.exe$)", {{
      { "d3d11.apitraceMode",               "True" },
    }} },
    /* Atelier series - games try to render video *
     * with a D3D9 swap chain over the DXGI swap  *
     * chain, which breaks D3D11 presentation     */
    { R"(\\Atelier_(Ayesha|Escha_and_Logy|Shallie)(_EN)?\.exe$)", {{
      { "d3d9.deferSurfaceCreation",        "True" },
    }} },
    /* Atelier Rorona/Totori/Meruru               */
    { R"(\\A(11R|12V|13V)_x64_Release(_en)?\.exe$)", {{
      { "d3d9.deferSurfaceCreation",        "True" },
    }} },
    /* Just how many of these games are there?    */
    { R"(\\Atelier_(Lulua|Lydie_and_Suelle|Ryza(_2)?)\.exe$)", {{
      { "d3d9.deferSurfaceCreation",        "True" },
    }} },
    /* ...                                        */
    { R"(\\Atelier_(Lydie_and_Suelle|Firis|Sophie)_DX\.exe$)", {{
      { "d3d9.deferSurfaceCreation",        "True" },
    }} },
    /* Fairy Tail                                 */
    { R"(\\FAIRY_TAIL\.exe$)", {{
      { "d3d9.deferSurfaceCreation",        "True" },
    }} },
    /* Nights of Azure                            */
    { R"(\\CNN\.exe$)", {{
      { "d3d9.deferSurfaceCreation",        "True" },
    }} },
    /* Star Wars Battlefront II: amdags issues    */
    { R"(\\starwarsbattlefrontii\.exe$)", {{
      { "dxgi.customVendorId",              "10de" },
    }} },
    /* F1 games - do not synchronize TGSM access  *
     * in a compute shader, causing artifacts     */
    { R"(\\F1_20(1[89]|[2-9][0-9])\.exe$)", {{
      { "d3d11.forceTgsmBarriers",          "True" },
    }} },
    /* Blue Reflection                            */
    { R"(\\BLUE_REFLECTION\.exe$)", {{
      { "d3d11.constantBufferRangeCheck",   "True" },
    }} },
    /* Secret World Legends                       */
    { R"(\\SecretWorldLegendsDX11\.exe$)", {{
      { "d3d11.constantBufferRangeCheck",   "True" },
    }} },
    /* Darksiders Warmastered - apparently reads  *
     * from write-only mapped buffers             */
    { R"(\\darksiders1\.exe$)", {{
      { "d3d11.apitraceMode",               "True" },
    }} },
    /* Monster Hunter World                       */
    { R"(\\MonsterHunterWorld\.exe$)", {{
      { "d3d11.apitraceMode",               "True" },
    }} },
    /* Kingdome Come: Deliverance                 */
    { R"(\\KingdomCome\.exe$)", {{
      { "d3d11.apitraceMode",               "True" },
    }} },
    /* Homefront: The Revolution                  */
    { R"(\\Homefront2_Release\.exe$)", {{
      { "d3d11.apitraceMode",               "True" },
    }} },
    /* Sniper Ghost Warrior Contracts             */
    { R"(\\SGWContracts\.exe$)", {{
      { "d3d11.apitraceMode",               "True" },
    }} },
    /* Shadow of the Tomb Raider - invariant      *
     * position breaks character rendering on NV  */
    { R"(\\SOTTR\.exe$)", {{
      { "d3d11.invariantPosition",          "False" },
      { "d3d11.floatControls",              "False" },
    }} },
    /* Nioh 2                                     */
    { R"(\\nioh2\.exe$)", {{
      { "dxgi.deferSurfaceCreation",        "True" },
    }} },
    /* DIRT 5 - uses amd_ags_x64.dll when it      *
     * detects an AMD GPU                         */
    { R"(\\DIRT5\.exe$)", {{
      { "dxgi.customVendorId",              "10de" },
    }} },
    /* Crazy Machines 3 - crashes on long device  *
     * descriptions                               */
    { R"(\\cm3\.exe$)", {{
      { "dxgi.customDeviceDesc",            "DXVK Adapter" },
    }} },
    /* World of Final Fantasy: Broken and useless *
     * use of 4x MSAA throughout the renderer     */
    { R"(\\WOFF\.exe$)", {{
      { "d3d11.disableMsaa",                "True" },
    }} },
    /* Final Fantasy XIV - Stuttering on NV       */
    { R"(\\ffxiv_dx11\.exe$)", {{
      { "dxvk.shrinkNvidiaHvvHeap",         "True" },
    }} },
    /* God of War - relies on NVAPI/AMDAGS for    *
     * barrier stuff, needs nvapi for DLSS        */
    { R"(\\GoW\.exe$)", {{
      { "d3d11.ignoreGraphicsBarriers",     "True" },
      { "d3d11.relaxedBarriers",            "True" },
      { "dxgi.nvapiHack",                   "False" },
    }} },
    /* AoE 2 DE - runs poorly for some users      */
    { R"(\\AoE2DE_s\.exe$)", {{
      { "d3d11.apitraceMode",               "True" },
    }} },

    /**********************************************/
    /* D3D9 GAMES                                 */
    /**********************************************/

    /* A Hat in Time                              */
    { R"(\\HatinTimeGame\.exe$)", {{
      { "d3d9.strictPow",                   "False" },
      { "d3d9.lenientClear",                "True" },
    }} },
    /* Anarchy Online                             */
    { R"(\\anarchyonline\.exe$)", {{
      { "d3d9.memoryTrackTest",             "True" },
    }} },
    /* Borderlands 2 and The Pre Sequel!           */
    { R"(\\Borderlands(2|PreSequel)\.exe$)", {{
      { "d3d9.lenientClear",                "True" },
      { "d3d9.supportDFFormats",            "False" },
    }} },
    /* Borderlands                                */
    { R"(\\Borderlands\.exe$)", {{
      { "d3d9.lenientClear",                "True" },
    }} },
    /* Gothic 3                                   */
    { R"(\\Gothic(3|3Final| III Forsaken Gods)\.exe$)", {{
      { "d3d9.supportDFFormats",            "False" },
    }} },
    /* Risen                                      */
    { R"(\\Risen[23]?\.exe$)", {{
      { "d3d9.invariantPosition",           "True" },
    }} },
    /* Sonic Adventure 2                          */
    { R"(\\Sonic Adventure 2\\(launcher|sonic2app)\.exe$)", {{
      { "d3d9.floatEmulation",              "False" },
    }} },
    /* The Sims 2,
       Body Shop,
       The Sims Life Stories,
       The Sims Pet Stories,
       and The Sims Castaway Stories             */
    { R"(\\(Sims2.*|TS2BodyShop|SimsLS|SimsPS|SimsCS)\.exe$)", {{
      { "d3d9.customVendorId",              "10de" },
      { "d3d9.customDeviceId",              "0091" },
      { "d3d9.customDeviceDesc",            "GeForce 7800 GTX" },
      { "d3d9.disableA8RT",                 "True" },
      { "d3d9.supportX4R4G4B4",             "False" },
      { "d3d9.maxAvailableMemory",          "2048" },
      { "d3d9.memoryTrackTest",             "True" },
    }} },
    /* Dead Space uses the a NULL render target instead
       of a 1x1 one if DF24 is NOT supported      */
    { R"(\\Dead Space\.exe$)", {{
      { "d3d9.supportDFFormats",                 "False" },
    }} },
    /* Halo 2                                     */
    { R"(\\halo2\.exe$)", {{
      { "d3d9.invariantPosition",           "True" },
    }} },
    /* Halo CE/HaloPC                             */
    { R"(\\halo(ce)?\.exe$)", {{
      { "d3d9.invariantPosition",           "True" },
      // Game enables minor decal layering fixes
      // specifically when it detects AMD.
      // Avoids chip being detected as unsupported
      // when on intel. Avoids possible path towards
      // invalid texture addressing methods.
      { "d3d9.customVendorId",              "1002" },
      // Avoids card not recognized error.
      // Keeps game's rendering methods consistent
      // for optimal compatibility.
      { "d3d9.customDeviceId",              "4172" },
      // The game uses incorrect sampler types in
      // the shaders for glass rendering which
      // breaks it on native + us if we don't
      // spec-constantly chose the sampler type
      // automagically.
      { "d3d9.forceSamplerTypeSpecConstants", "True" },
      { "rtx.lightmapTextures",  "211F65249E6D4837, 60CD2BCF8482B187,992CC729B6D67939,9994DEAFA52F35CD,A0068A9A5106777A,AE968ECEAC209AFF,CE59061AA5CCAE8B,DF6558A0EF71AC4B,E601679429E67BA3,EC2CC02D0C24CACE,FF3A56BDAA5FE64D" },
      { "rtx.skyBoxTextures",    "" },
      { "rtx.ignoreTextures",    "0" },
      { "rtx.uiTextures",        "" },
      { "rtx.useObsoleteHashOnTextureUpload", "True" },
    }} },
    /* Counter Strike: Global Offensive
       Needs NVAPI to avoid a forced AO + Smoke
       exploit so we must force AMD vendor ID.    */
    { R"(\\csgo\.exe$)", {{
      { "d3d9.customVendorId",              "1002" },
    }} },
    /* Vampire - The Masquerade Bloodlines        */
    { R"(\\vampire\.exe$)", {{
      { "d3d9.deferSurfaceCreation",        "True" },
      { "d3d9.memoryTrackTest",             "True" },
      { "d3d9.maxAvailableMemory",          "1024" },
    }} },
    /* Senran Kagura Shinovi Versus               */
    { R"(\\SKShinoviVersus\.exe$)", {{
      { "d3d9.forceAspectRatio",            "16:9" },
    }} },
    /* Metal Slug X                               */
    { R"(\\mslugx\.exe$)", {{
      { "d3d9.supportD32",                  "False" },
    }} },
    /* Skyrim (NVAPI)                             */
    { R"(\\TESV\.exe$)", {{
      { "d3d9.customVendorId",              "1002" },
    }} },
    /* RTHDRIBL Demo                              
       Uses DONOTWAIT after GetRenderTargetData
       then goes into an infinite loop if it gets
       D3DERR_WASSTILLDRAWING.
       This is a better solution than penalizing
       other apps that use this properly.         */
    { R"(\\rthdribl\.exe$)", {{
      { "d3d9.allowDoNotWait",              "False" },
    }} },
    /* Hyperdimension Neptunia U: Action Unleashed */
    { R"(\\Neptunia\.exe$)", {{
      { "d3d9.forceAspectRatio",            "16:9" },
    }} },
    /* D&D - The Temple Of Elemental Evil          */
    { R"(\\ToEE\.exe$)", {{
      { "d3d9.allowDiscard",                "False" },
    }} },
    /* ZUSI 3 - Aerosoft Edition                  */
    { R"(\\ZusiSim\.exe$)", {{
      { "d3d9.noExplicitFrontBuffer",       "True" },
    }} },
    /* GTA IV (NVAPI)                             */
    /* Also thinks we're always on Intel          *
     * and will report/use bad amounts of VRAM.   */
    { R"(\\GTAIV\.exe$)", {{
      { "d3d9.customVendorId",              "1002" },
      { "dxgi.emulateUMA",                  "True" },
    }} },
    /* Battlefield 2 (bad z-pass)                 */
    { R"(\\BF2\.exe$)", {{
      { "d3d9.longMad",                     "True" },
      { "d3d9.invariantPosition",           "True" },
    }} },
    /* SpellForce 2 Series                        */
    { R"(\\SpellForce2.*\.exe$)", {{
      { "d3d9.forceSamplerTypeSpecConstants", "True" },
    }} },
    /* Everquest 2                                */
    { R"(\\EverQuest2.*\.exe$)", {{
      { "d3d9.alphaTestWiggleRoom", "True" },
    }} },
    /* Tomb Raider: Legend                       */
    { R"(\\trl\.exe$)", {{
      { "d3d9.apitraceMode",                "True" },
    }} },
    /* Everquest                                 */
    { R"(\\eqgame\.exe$)", {{
      { "d3d9.apitraceMode",                "True" },
    }} },
    /* Dark Messiah of Might & Magic             */
    { R"(\\mm\.exe$)", {{
      { "d3d9.deferSurfaceCreation",        "True" },
      { "d3d9.memoryTrackTest",             "True" },
    }} },
    /* Mafia 2                                   */
    { R"(\\mafia2\.exe$)", {{
      { "d3d9.customVendorId",              "10de" },
      { "d3d9.customDeviceId",              "0402" },
    }} },
    /* Warhammer: Online                         */
    { R"(\\WAR(-64)?\.exe$)", {{
      { "d3d9.customVendorId",              "1002" },
    }} },
    /* Dragon Nest                               */
    { R"(\\DragonNest_x64\.exe$)", {{
      { "d3d9.memoryTrackTest ",            "True" },
    }} },
    /* Dal Segno                                 */
    { R"(\\DST\.exe$)", {{
      { "d3d9.deferSurfaceCreation",        "True" },
    }} },
    /* HL2 engine                              */
    { R"(\\hl2\.exe$)", {{
      { "rtx.baseGameModRegex", std::string("sourcemods") },
      { "rtx.baseGameModPathRegex", std::string("-game \"([a-zA-Z]:.*sourcemods.*)\"") },
      { "rtx.showRaytracingOption", "False" },
      { "rtx.lightConverter",
        "2ef850e6fbfd8c87,"
        "11bdb0aec66e413a,"
        "5d2b45a0e4d62133,"
        "1d7191114ae3cab2,"
        "2c53b6d1412d82ee,"
        "6a7de931f906f159,"
        "8f877d11c0c69b09,"
        "e7afd1a8e179429b,"
        "c02fe462ba62838a,"
        "edda43a7194b6597,"
        "460306e97fb2d4b5,"
        "bcd2ca5224499175,"
        "2d5ac1adc56a42fd,"
        "b847641d0db70d7a,"
        "6d1c7640f8e75e57,"
        "3c9d70691e07b676,"
        "81adda2b5d6af17b,"
        "94c8baa2be97e3a, "
        "d6a162813f232ec5,"
        "407b3900391f92bb,"
        "87dfcd7146139c4a,"
        "7dc0376066ac76bc,"
        "687c5f75f2c8d860,"
        "cdeb53e58a94a92b,"
        "7eb3f191000f642a,"
        "de77f4f94de3dfc3,"
        "4fd7aea93bcc3833,"
        "f4a7f6be329029ca,"
        "a3979feff1010a75,"
        "89d244855965b001,"
        "3b2c78b35e0a88d5,"
        "f9387048e84ace7c,"
        "e88e5843fd107382,"
        "d0425187257ec023,"
        "9158e4ac55129ecb,"
        "aa16c42fa367111c"
      },
      { "rtx.lightmapTextures",
        "441282E47FE7CB64,"
        "050173DFF733DBE1,"
        "913E194A071E2720,"
        "8D39476483C92F63,"
        "8BF8566E3C8B006F,"
        "54392326AF548522,"
        "AF312EE92AAD9609,"
        "A66CF9B74461F3DE,"
        "BD904729C36EEFE8,"
        "DB0E52AC3A7C8F12,"
        "C44E50BC6B433C6E,"
        "EE61086C4E281087,"
        "5371BABD1CDD7707,"
        "E4CFD8B693D251DD,"
        "BDD5ACBF489E7853,"
        "7B16D306254AB39E,"
        "C12951B8D7192A9E,"
        "7DE4CAEA279A9A09,"
        "5AF15B44D7E92568,"
        "CFB3E770A1FAF3F2,"
        "CDC07C4F6BD631F5,"
        "1B8F23FC10195395,"
        "8D6ACC0820F0D424,"
        "FAD3E22AA96D7B51,"
        "E9BEA521567E008E,"
        "AA2B1355C046AA80,"
        "26172AB99925C7AD,"
        "7B16D306254AB39E,"
        "7DE4CAEA279A9A09,"
        "BDD5ACBF489E7853,"
        "C12951B8D7192A9E,"
        "5AF15B44D7E92568,"
        "264724EA902655F8,"
        "E51A3DB8B4CE10AF,"
        "C8DECC54A0085620,"
        "33D41383CC45BCE1,"
        "CE19CDCC72E90FDF,"
        "08790A05BF2829FB,"
        "5F7DE781C2993BEC,"
        "A60D1CF2839DC373,"
        "FEE6D3D200FF4220,"
        "216C17AA33DB5D75"
      },
      { "rtx.uiTextures",
        "7C47908363E9FB46,"
        "49C49D3F95609C9D,"
        "0ACC5AF8C7A6A72C,"
        "25F9AA6D11F0F1E0,"
        "0F707B765176FA99,"
        "A0B13F306011D748,"
        "71C560B061683B20,"
        "b1efb6a865b3082c,"
        "aedb4949ec308638," 
        "BC840D956C24C33C,"
        "010500D8F9BC71A1,"
        "65C067A6504C559E,"
        "7E20D6C917522EE4,"
        "1F06EE8596B7DD41,"
        "1FB0EBA5FEBD1B5A,"
        "C0F138D79131F8C1" // cake splash screen (after beating the game)
      },
      { "rtx.ignoreTextures",
        // Fake lighting strips present around borders of the chambers
        "2EF850E6FBFD8C87,"  
        // Various glowing sprites/light shafts that we want to replace with modern effects
        "2F40734A713AABCE,"
        "C5D5E4DC2C8B16A4,"
        "193E96F2A664E570,"
        "4BEE64E543B72DE8,"
        "A73A56119D34B9A8,"
        "CCF171A5B95F42AC,"
        "3588CCE077177F37,"
        "C511630F7EE7383C,"
        "0B0B3516EC8F2672,"
        "CA4AA3441DAA53CA,"
        "1A82AD51BADE42C6,"
        "525F90354488C30B,"
        "BAD0E1288F3F5A5A,"
        "6A7DE931F906F159,"
        "E7AFD1A8E179429B,"
        "C02FE462BA62838A,"
        "EDDA43A7194B6597,"
        "11BDB0AEC66E413A,"
        "260EAE29EC4727F3,"
        "A0EF42611EFCDBA5,"
        "6010A18E22F8CE34,"
        "A08B874535052615,"
        "8AA105C2149F4119,"
        "EFAC5FE5EB531111,"
        "A0EF42611EFCDBA5,"  // Character - Chell Checkerboard pattern used on one of the eye passes (unknown material?)
        "8AA105C2149F4119,"  // ugly textures used for energy balls sliced by portals
        "068E64C3DB849782,"  // ugly textures used for plasma catchers for fake bloom
        "92e275beee2d2c12,"  // tanker detail texture
        "ace20008ae3a0a5b,"  // barrel detail texture
      },
      { "rtx.ignoreLights",
        // Chell atlases - associated with a light (player light?)
        "460306E97FB2D4B5,"  // 5044883607524660405ULL
        "2D5AC1ADC56A42FD,"  // 3268137431696294653ULL
      },
      // TREX-469 workaround: hide unwanted objects
      // Used temporarily to disable meshes pending feature work, without breaking capture tests
      { "rtx.hideInstanceTextures",
        ""
      },
      { "rtx.playerModelTextures",
        // Character - Chell
        "8DD6F568BD126398,"  // Left eye
        "EEF8EFD4B8A1B2A5,"  // Right eye
        "4A066E5A5292D273,"  // Hair and eyelashes
        "AC869B6F32D8BBDB,"  // Something in the eyes
        "2D5AC1ADC56A42FD,"  // Body
        "E53AE01AC1FF9E03,"  // Head
        "9FC25F8E3D685EA5,"  // Held portal gun
        "9DED9E2A03234E95,"  // Gun particles
        "EEEF6F901EEE1164,"  // Gun particles
        "4DEEF5C779DDC88A,"  // Gun particles
        "3CD4F0E2A8AAD575,"  // Gun particles
        "F2A8C629EF1809C3,"  // Gun particles
        "4FEB275B85245FB9,"  // Gun particles
        // Chell - Medium texture detail
        "1BE7E510328AB010,"  // Body
        "2AF8E51AAA752D40,"
        "234A8CD5F00F220D,"
        "3B21664B1B19F463,"
        "3A349F1B5FD0B874,"
        "1E94FE2ABE6A3777,"
        "D2B78D811954C600,"
        "571EE878F3238A3F,"
        // Chell - Low texture detail
        "DC8E4C587DF53D4C,"  // Body
        "CBDCB2327A1BB55B,"
        "163DCDE80551AFE2,"
        "7957972EDFF7EECC,"
        "17AE2077ADBE2A57,"
        "959F1B8A7563FDBB,"
        "126DEB020C4E0D2D,"
        "C3F3985DC82F765E,"
        "7D177970C35D7225,"
      },
      { "rtx.playerModelBodyTextures",
        "2D5AC1ADC56A42FD," // Chell - high texture detail
        "1BE7E510328AB010," // Chell - medium texture detail
        "DC8E4C587DF53D4C," // Chell - low texture detail
      },
      { "rtx.particleTextures",
        "C0BE016F97F55259," // steam
        "2CB02C7BB3702A1F," // collision dust
        "F12275CBAFC9CA75," // collision dust
        "12A8733BBDF0FE20," // bullet hit particles
        "3AEFA6FD5CF2DEB4," // rope/cables
        "9F874078BE0C83FF," // rope/cables
        "A5153B06569D6510," // glados defeat particle
        "CD28C5A663826A6C," // glados defeat particle
        "9C5D83E7E6B76A7A," // glados defeat swirly black particles
        "F54E5ECA2E1504FD," // end game smoke
        "577C6F86C18AAAA5," // portal opening flash
        "EEEF6F901EEE1164," // glowing circle trail from portal gun fire
        "F3DF557E6DDC103C," // underwater particles
        "554AE68A890A90FB," // underwater particles
        "FFC88527F4693A87," // tiny elevator particle stream component
        "9DED9E2A03234E95," // portal gun particles
        "4DEEF5C779DDC88A," // portal gun particles
        "bd6fe490eca6a50f," // turret bullet particles
        "63FF8A68ADB06117," // portal ring particles
        "05054E94DD6BB441," // portal ring particles
        "0C50217D8C6FDCC2," // plasma catcher electric effect
        "49E4EC22E559AFC2," // plasma catcher electric effect
        "280AC336CFC68401," // plasma catcher electric effect
        "394800E61100412F," // plasma catcher electric effect
        "285E8D0537EBEBA1," // plasma catcher electric effect
        "7862B129760B74F0," // plasma catcher electric effect
        "6253F3CDC90DC6FB," // plasma catcher electric effect
        "69E5FE25984A5529," // plasma catcher electric effect
        "FFCE11F1540354CA," // plasma catcher electric effect
        "232AE6FEF8EEF0BD," // plasma catcher electric effect
        "105F7D19ED93147E," // platform pillar top effect
      },
      { "rtx.beamTextures",
        "ad7af1c4fca862e4," // nonstationary platform track beam
        "f116b8e9da308ee8," // turret laser
        "059b0044c2e2d9dd," // rocket turret laser
      },
      { "rtx.decalTextures",
        "0464EB8194DD2139,"
        "077416B246F7EBF9,"
        "0D21C78830B9B87E,"
        "0E0905D9231B2621,"
        "0F4986B12FBC9B10,"
        "1CC7CA1FD5C7CEBC,"
        "2288A5A74C035053,"
        "25AF94A27B585E5A,"
        "2DBF6CC9A5652816,"
        "2F38DA65B73883EE,"
        "35961208D8AA165B,"
        "37AF209A1A371D8F,"
        "3CE13ABFA28FB599,"
        "3DB98F1B93F4679A,"
        "40D969C3B7B837F2,"
        "474E1B6A2EA8F082,"
        "4E9D342DFAD12947,"
        "4F33C5B2342FA20B,"
        "5073D083DAE15E8B,"
        "508B88AC09F56141,"
        "51BD52AFFAAD4BE7,"
        "53841B078528D4EF,"
        "58DFED2F17277010,"
        "60F5B0BF449D5C5C,"
        "6643F8FF7C42CB18,"
        "6C9DB83C1D5A5254,"
        "7A619D021C573F04,"
        "7FE3253F3EC79C0D,"
        "810643D7974355CF,"
        "8B3FA1ED9319A08B,"
        "8D0AACAE9911101A,"
        "8DA1232E36B0AB4D,"
        "90B63328CD155524,"
        "9B35406FACCF2C8F,"
        "A5D050857A01EE5D,"
        "AAAB0CB0C06F9934,"
        "AE6FC0599B192217,"
        "B0BA2CC643F93597,"
        "B68F559B25BF12AE,"
        "B798B753E4B43330,"
        "B93C3AF34B6F3980,"
        "C045D91DACCA62EB,"
        "C3BA8F2EC836E2B1,"
        "C41860E9CD66844C,"
        "C805C1C433BE9CC3,"
        "C9603739E8F2686B,"
        "C97FD37AF7708F22,"
        "D466A216C1A295DA,"
        "D51BD114D87C00BD,"
        "E0062D64AC9BAC08,"
        "E37B04B0085B6401,"
        "E83D04C31FE08619,"
        "E9FD72BAAB0C5FD0,"
        "EF607C1AF136DF26,"
        "F4661A1B6AA2E97B,"
        "F600C3C5174DBF69,"
        "F974DA687E700B25,"
        "FAD5EEA07EE81FCA,"
        "FBF1F662D1232979,"
        "FCF7F7862B76C49F,"
        "FF487E33FC613B9C,"
        "ED9A4736E697A97B,"
        "5585E3941BBD8A30,"
        "27C8BA6D1FB47A6A,"
        "6F1EAF2F9481C02F,"
        "121AF2BCC5B5AFCA,"
        "215BAAFC5A07B208,"
        "739825af5ff7b600," // end game asphalt
        "8f622d6d3b46b751," // end game asphalt
         // Previously "rtx.dynamicDecalTextures", merged into this list
        "f017847a501d804b," // blood and bullet holes
        "a65293be7ea5f7b7," // plasma ball burn marks etc.
         // Previously "rtx.nonOffsetDecalTextures", merged into this list
        "727B75DD886D94FD," // Alyx eye irises
        "64A2E9E0169AE37F,"
        "C4826ABA6336F7FF," // Citizen NPC eye irises
        "5E53185FD64EEFF2,"
      },
      { "rtx.worldSpaceUiTextures",
       // Challenge map score boards
       "2F0654813BA4509B,"
       "E5A693D8A8BE5D34,"
       "62902E857F4B7230,"
       "38F233758BDF24F1,"
       "D764F53F9492150B,"
       "03027CD7C8492876,"
       "20D8A0C0EF108A33,"
       "49B4A977C4971EBC,"
       "34EBDE3214C50C43,"
       "FCECEDB4661B60EE,"
       "903E97BD3086C74B,"
       "CA8490701F86CB04,"
       "353CEC6EDBFBE689,"
       "28CB3CB457979BDC,"
       "49A6D4FA0F562B5C,"
       "1954FDFC34E8D819,"
       "9C16F4679F33F113,"
       "87770D9B57CED8C1,"
       "C7E5ED72431C4A6F,"
      },
      { "rtx.worldSpaceUiBackgroundTextures",
        "ece63a6d1de44f11" // Monitors with countdown timer in the GLaDOS chamber
      },
      { "rtx.skyBoxTextures",
        "ED271AB781D49A9A,"
        "3574F482B41905E8,"
        "C5C302766FA5F91D,"
        "B25CD04A355C45D9,"
        "BD2CBBFAECF0168C,"
        "9083D293A167C5B,"
        // Trees that drawn using a different view matrix
        "6CB534F9ACD206D5,"
        "CA4F5DA4FBB99FFC,"
      },
      { "rtx.animatedWaterTextures",
        "522E5513DB9638B6,"
      },        
      { "rtx.zUp",                   "True" },
      { "rtx.uniqueObjectDistance",  "300.0" }, // Game is 1unit=1cm - picking up objects can move them very quickly, 3m should be sufficient.
      { "rtx.rayPortalModelTextureHashes",        "5EC61BC800744B26, DFDACB6DE1C7741E" }, // Orange and Blue Portal textures
      { "rtx.rayPortalEnabled",                   "True" },
      { "rtx.rayPortalModelNormalAxis",           "1.0, 0.0, 0.0" },
      { "rtx.rayPortalModelWidthAxis",            "0.0, 1.0, 0.0" },
      { "rtx.rayPortalModelHeightAxis",           "0.0, 0.0, 1.0" },
      { "rtx.rayPortalSamplingWeightMinDistance", "100.0" },
      { "rtx.rayPortalSamplingWeightMaxDistance", "10000.0" },
      { "rtx.rayPortalCameraHistoryCorrection",   "True" },
      { "rtx.rayPortalCameraInBetweenPortalsCorrection",   "True" },
      { "rtx.viewModel.enable",                   "True" },
      { "rtx.viewModel.viewRelativeOffsetMeters", "0.005, -0.002, -0.055" },
      { "rtx.viewModel.scale",                    "0.4" },
      { "rtx.effectLightPlasmaBall",              "True" },
      { "rtx.enableVolumetricLighting",           "True" },
      { "rtx.secondarySpecularFireflyFilteringThreshold",  "120.0" },
      { "rtx.volumetricTransmittanceColor",       "0.953238, 0.948409, 0.943550" }, // Slight blue tint to act more like water vapor for now
      { "rtx.volumetricTransmittanceMeasurementDistance", "20000.0" },
      { "rtx.froxelGridResolutionScale",          "16" },
      { "rtx.froxelDepthSlices",                  "48" },
      { "rtx.enableFogRemap",                     "True" },
      { "rtx.fogRemapMaxDistanceMin",             "100.0" },
      { "rtx.fogRemapMaxDistanceMax",             "4000.0" },
      { "rtx.fogRemapTransmittanceMeasurementDistanceMin", "2000.0" },
      { "rtx.fogRemapTransmittanceMeasurementDistanceMax", "12000.0" },
      { "rtx.useObsoleteHashOnTextureUpload",                   "True" },
      { "rtx.temporalAA.maximumRadiance",                       "10000.0" },
      { "rtx.temporalAA.colorClampingFactor",                   "1.0"  },
      { "rtx.temporalAA.newFrameWeight",                        "0.05" },
      { "rtx.postfx.motionBlurSampleCount",                     "4"     },
      { "rtx.postfx.exposureFraction",                          "0.4"   },
      { "rtx.postfx.blurDiameterFraction",                      "0.02"  },
      { "rtx.postfx.motionBlurMinimumVelocityThresholdInPixel", "1.5"   },
      { "rtx.postfx.motionBlurDynamicDeduction",                "0.075" },
      { "rtx.postfx.motionBlurJitterStrength",                  "0.6"   },
      { "rtx.postfx.enableMotionBlurNoiseSample",               "True"  },
      { "rtx.postfx.chromaticAberrationAmount",                 "0.0"   },
      { "rtx.postfx.chromaticCenterAttenuationAmount",          "0.975" },
      { "rtx.postfx.vignetteIntensity",                         "1.0"   },
      { "rtx.postfx.vignetteRadius",                            "0.8"   },
      { "rtx.postfx.vignetteSoftness",                          "0.1"   },
      { "rtx.enableNearPlaneOverride",                          "True"  },
      { "rtx.nativeMipBias",                                    "0.9"   },
      { "rtx.upscalingMipBias",                                 "-0.4"  },
      { "rtx.legacyMaterial.roughnessConstant",                 "0.1"   },
      { "rtx.opacityMicromap.enable",                           "True"  },
      { "rtx.decals.maxOffsetIndex",                            "64" },
      // TODO (REMIX-656): Remove this once we can transition content to new hash
      { "rtx.geometryGenerationHashRuleString", "positions,"
                                                "indices,"
                                                "texcoords,"
                                                "legacypositions0,"
                                                "legacypositions1,"
                                                "legacyindices,"
                                                "geometrydescriptor,"
                                                "vertexlayout" },
        { "rtx.allowCubemaps",                  "True" },
    }} },
    /* Kohan II                                  */
    { R"(\\k2\.exe$)", {{
      { "d3d9.memoryTrackTest",             "True" },
    }} },
    /* Ninja Gaiden Sigma 1/2                    */
    { R"(\\NINJA GAIDEN SIGMA(2)?\.exe$)", {{
      { "d3d9.deferSurfaceCreation",        "True" },
    }} },
    /* Demon Stone breaks at frame rates > 60fps */
    { R"(\\Demonstone\.exe$)", {{
      { "d3d9.maxFrameRate",                "60" },
    }} },
    /* Far Cry 1 has worse water rendering when it detects AMD GPUs */
    { R"(\\FarCry\.exe$)", {{
      { "d3d9.customVendorId",              "10de" },
    }} },
    /* Earth Defense Force 5 */
    { R"(\\EDF5\.exe$)", {{
      { "dxgi.tearFree",                    "False" },
      { "dxgi.syncInterval",                "1"     },
    }} },
    /* Sine Mora EX */
    { R"(\\SineMoraEX\.exe$)", {{
      { "d3d9.maxFrameRate",                "60" },
    }} },
    /* Fantasy Grounds                           */
    { R"(\\FantasyGrounds\.exe$)", {{
      { "d3d9.noExplicitFrontBuffer",       "True" },
    }} },
    /* Red Orchestra 2                           */
    { R"(\\ROGame\.exe$)", {{
      { "d3d9.floatEmulation",              "Strict" },
    }} },
    /* Bully: Scholarship Edition uses three untextured calls for its skybox */
    { R"(\\Bully\.exe$)",{{
      { "rtx.skyDrawcallIdThreshold",       "3"      },
    }} },
    /* Driver: Parallel Lines crash prevention  */
    { R"(\\DriverParallelLines\.exe$)",{{
      { "d3d9.deferSurfaceCreation",       "False"   },
    }} },
    /* Sword and Fairy 4 flickering fix         */
    { R"(\\PAL4\.exe$)",{{
    { "d3d9.noExplicitFrontBuffer",        "True"    },
    }} },
    /* Dark Souls II                            */
    { R"(\\DarkSoulsII\.exe$)", {{
      { "d3d9.floatEmulation",              "Strict" },
    }} },
    /* Dogfight 1942                            */
    { R"(\\Dogfight1942\.exe$)", {{
      { "d3d9.floatEmulation",              "Strict" },
    }} },
    /* Bayonetta                                */
    { R"(\\Bayonetta\.exe$)", {{
      { "d3d9.floatEmulation",              "Strict" },
    }} },
    /* Rayman Origins                           */
    { R"(\\Rayman Origins\.exe$)", {{
      { "d3d9.floatEmulation",              "Strict" },
    }} },
    /* Guilty Gear Xrd -Relevator-              */
    { R"(\\GuiltyGearXrd\.exe$)", {{
      { "d3d9.floatEmulation",              "Strict" },
    }} },
    /* Richard Burns Rally                      */
    { R"(\\RichardBurnsRally_SSE\.exe$)", {{
      { "d3d9.floatEmulation",              "Strict" },
    }} },
    /* BlazBlue Centralfiction                  */
    { R"(\\BBCF\.exe$)", {{
      { "d3d9.floatEmulation",              "Strict" },
    }} },
    /* James Cameron's Avatar needs invariantPosition to fix black flickering vegetation */
    { R"(\\Avatar\.exe$)", {{
      { "d3d9.invariantPosition",              "True" },
    }} },
  }};


  static bool isWhitespace(char ch) {
    return ch == ' ' || ch == '\x9' || ch == '\r';
  }

  
  static bool isValidKeyChar(char ch) {
    return (ch >= '0' && ch <= '9')
        || (ch >= 'A' && ch <= 'Z')
        || (ch >= 'a' && ch <= 'z')
        || (ch == '.' || ch == '_');
  }


  static size_t skipWhitespace(const std::string& line, size_t n) {
    while (n < line.size() && isWhitespace(line[n]))
      n += 1;
    return n;
  }


  struct ConfigContext {
    bool active;
  };


  static void parseUserConfigLine(Config& config, ConfigContext& ctx, const std::string& line) {
    std::stringstream key;
    std::stringstream value;

    // Extract the key
    size_t n = skipWhitespace(line, 0);

    if (n < line.size() && line[n] == '[') {
      n += 1;

      size_t e = line.size() - 1;
      while (e > n && line[e] != ']')
        e -= 1;

      while (n < e)
        key << line[n++];
      
      ctx.active = key.str() == env::getExeName();
    } else {
      while (n < line.size() && isValidKeyChar(line[n]))
        key << line[n++];
      
      // Check whether the next char is a '='
      n = skipWhitespace(line, n);
      if (n >= line.size() || line[n] != '=')
        return;

      // Extract the value
      bool insideString = false;
      n = skipWhitespace(line, n + 1);

      while (n < line.size()) {
        // NV-DXVK start: allow white-space in vector entries within a line
        // NV-DXVK end
        if (line[n] == '"') {
          insideString = !insideString;
          n++;
        } else
          value << line[n++];
      }
      
      if (ctx.active)
        config.setOption(key.str(), value.str());
    }
  }

  // NV-DXVK start: Configuration parsing logic moved out for sharing between multiple configuration loading functions
  static Config parseConfigFile(std::string filePath) {
    Config config;
    
    // Open the file if it exists
    std::ifstream stream(str::tows(filePath.c_str()).c_str());

    if (!stream) {
      Logger::info(str::format("No config file found at: ", filePath));
      return config;
    }
    
    // Inform the user that we loaded a file, might
    // help when debugging configuration issues
    Logger::info(str::format("Found config file: ", filePath));

    // Initialize parser context
    ConfigContext ctx;
    ctx.active = true;

    // Parse the file line by line
    std::string line;

    while (std::getline(stream, line))
      parseUserConfigLine(config, ctx, line);
    
    Logger::info("Parsed config file.");
    return config;
  }
  // NV-DXVK end

  Config::Config() { }
  Config::~Config() { }


  Config::Config(OptionMap&& options)
  : m_options(std::move(options)) { }


  void Config::merge(const Config& other) {
    for (auto& pair : other.m_options)
      m_options[pair.first] = pair.second;
  }

  // NV-DXVK start: new methods
  std::string Config::generateOptionString(const bool& value) {
    return value ? std::string("True") : std::string("False");
  }

  std::string Config::generateOptionString(const int32_t& value) {
    return std::to_string(value);
  }

  std::string Config::generateOptionString(const uint32_t& value) {
    return std::to_string(value);
  }

  std::string Config::generateOptionString(const float& value) {
    std::stringstream ss;
    ss << value;
    return ss.str();
  }

  std::string Config::generateOptionString(const Vector2i& value) {
    std::stringstream ss;
    ss << value.x << ", " << value.y;
    return ss.str();
  }

  // NV-DXVK start: added a variant
  std::string Config::generateOptionString(const Vector2& value) {
    std::stringstream ss;
    ss << value.x << ", " << value.y;
    return ss.str();
  }
  // NV-DXVK end

  std::string Config::generateOptionString(const Vector3& value) {
    std::stringstream ss;
    ss << value.x << ", " << value.y << ", " << value.z;
    return ss.str();
  }

  // NV-DXVK start: added a variant
  std::string Config::generateOptionString(const Vector4& value) {
    std::stringstream ss;
    ss << value.x << ", " << value.y << ", " << value.z << ", " << value.w;
    return ss.str();
  }
  // NV-DXVK end

  std::string Config::generateOptionString(const Tristate& value) {
    switch (value) {
    default:
    case Tristate::Auto: return "Auto";
    case Tristate::False: return "False";
    case Tristate::True: return "True";
    }
  }
  // NV-DXVK end

  void Config::setOption(const std::string& key, const std::string& value) {
    m_options.insert_or_assign(key, value);
  }

  void Config::setOption(const std::string& key, const bool& value) {
    setOption(key, generateOptionString(value));
  }

  void Config::setOption(const std::string& key, const int32_t& value) {
    setOption(key, generateOptionString(value));
  }

  void Config::setOption(const std::string& key, const uint32_t& value) {
    setOption(key, generateOptionString(value));
  }

  void Config::setOption(const std::string& key, const float& value) {
    setOption(key, generateOptionString(value));
  }

  void Config::setOption(const std::string& key, const Vector2i& value) {
    setOption(key, generateOptionString(value));
  }

  // NV-DXVK start: added a variant
  void Config::setOption(const std::string& key, const Vector2& value) {
    setOption(key, generateOptionString(value));
  }
  // NV-DXVK end

  void Config::setOption(const std::string& key, const Vector3& value) {
    setOption(key, generateOptionString(value));
  }

  void Config::setOption(const std::string& key, const Tristate& value) {
    setOption(key, generateOptionString(value));
  }

  std::string Config::getOptionValue(const char* option) const {
    auto iter = m_options.find(option);

    return iter != m_options.end()
      ? iter->second : std::string();
  }

  bool Config::parseOptionValue(
    const std::string&  value,
          std::string&  result) {
    if (value.size() == 0)
      return false;
    result = value;
    return true;
  }

  bool Config::parseOptionValue(
    const std::string& value,
    std::vector<std::string>& result) {
    std::stringstream ss(value);
    std::string s;
    while (std::getline(ss, s, ',')) {
      result.push_back(s);
    }
    return true;
  }

  bool Config::parseOptionValue(
    const std::string&  value,
          bool&         result) {
    // NV-DXVK start: Allow 1 and 0 for true/false options
    static const std::array<std::pair<const char*, bool>, 4> s_lookup = {{
      { "true",  true  },
      { "false", false },
      { "1",  true  },
      { "0", false },
    }};
    // NV-DXVK end

    return parseStringOption(value,
      s_lookup.begin(), s_lookup.end(), result);
  }

  bool Config::parseOptionValue(
    const std::string&  value,
          int32_t&      result) {
    if (value.size() == 0)
      return false;

    // NV-DXVK start: skip whitespaces at start of number strings
    try {
      result = std::stoi(value);
      return true;
    }
    catch (...) {
      return false;
    }
    // NV-DXVK end
  }

  bool Config::parseOptionValue(
    const std::string& value,
          uint32_t&    result) {
    if (value.size() == 0)
      return false;

    // NV-DXVK start: skip whitespaces at start of number strings
    try {
      result = std::stol(value);
      return true;
    }
    catch (...) {
      return false;
    }
    // NV-DXVK end
  }

  bool Config::parseOptionValue( 
    const std::string&  value,
          float&        result) {
    if (value.size() == 0)
      return false;

    // NV-DXVK start: handle invalid inputs
    try {
      result = std::stof(value);
      return true;
    }
    catch (...) {
      return false;
    }
    // NV-DXVK end
  }

  bool Config::parseOptionValue(
    const std::string& value,
    Vector2i& result) {
    std::stringstream ss(value);
    std::string s;
    for (int i = 0; i < 2; ++i) {
      if (!std::getline(ss, s, ',')) {
        return false;
      }

      int value;
      if (!parseOptionValue(s, value)) {
        return false;
      }

      result[i] = value;
    }

    return true;
  }

  // NV-DXVK start: added a variant
  bool Config::parseOptionValue(
    const std::string& value,
    Vector2& result) {
    std::stringstream ss(value);
    std::string s;
    for (int i = 0; i < 2; ++i) {
      if (!std::getline(ss, s, ',')) {
        return false;
      }

      float value;
      if (!parseOptionValue(s, value)) {
        return false;
      }

      result[i] = value;
    }

    return true;
  }
  // NV-DXVK end

  bool Config::parseOptionValue(
    const std::string& value,
    Vector3& result) {
    std::stringstream ss(value);
    std::string s;
    for (int i = 0; i < 3; ++i) {
      if (!std::getline(ss, s, ',')) {
        return false;
      }

      float value;
      if (!parseOptionValue(s, value)) {
        return false;
      }

      result[i] = value;
    }

    return true;
  }
  
  bool Config::parseOptionValue(
    const std::string& value,
    VirtualKeys& result) {
    std::stringstream ss(value);
    std::string s;
    bool bFoundValidConfig = false;
    VirtualKeys virtKeys;
    while (std::getline(ss, s, ',')) {
      VirtualKey vk;
      if(s.find("0x") != std::string::npos) {
        VkValue vkVal = std::stoul(s, nullptr, 16);
        vk.val = vkVal;
      } else {
        vk = KeyBind::getVk(s);
      }
      if(!KeyBind::isValidVk(vk)) {
        bFoundValidConfig = false;
        break;
      }
      virtKeys.push_back(vk);
      bFoundValidConfig = true;
    }
    if(bFoundValidConfig) {
      result = std::move(virtKeys);
    }
    return bFoundValidConfig;
  }
  
  bool Config::parseOptionValue(
    const std::string&  value,
          Tristate&     result) {
    static const std::array<std::pair<const char*, Tristate>, 3> s_lookup = {{
      { "true",  Tristate::True  },
      { "false", Tristate::False },
      { "auto",  Tristate::Auto  },
    }};

    return parseStringOption(value,
      s_lookup.begin(), s_lookup.end(), result);
  }


  template<typename I, typename V>
  bool Config::parseStringOption(
          std::string   str,
          I             begin,
          I             end,
          V&            value) {
    str = Config::toLower(str);

    for (auto i = begin; i != end; i++) {
      if (str == i->first) {
        value = i->second;
        return true;
      }
    }

    return false;
  }

  // NV-DXVK start: Generic config parsing, reduce duped code
  template<Config::Type type>
  Config Config::getConfig(const std::string& configPath) {
    const Desc& desc = getDesc(type);
    const std::string envVarName(desc.env);
    const std::string envVarPath = !envVarName.empty() ? env::getEnvVar(envVarName.c_str()) : "";
    Logger::info(str::format("Looking for config: ", desc.name));
    // Getting a default "App" Config doesn't require parsing a file.
    if constexpr(type == Type_App) {
      const auto exePath = env::getExePath();
      return getAppConfig(exePath);
    // A previous conf file has explicitly stated a future conf file must be used...
    } else if(!configPath.empty()) {
      const std::string filePath = configPath + "/" + desc.confName;
      Logger::info(str::format("Attempting to parse: ", filePath, "..."));
      return parseConfigFile(filePath);
    // A relevant env var has been set
    } else if (!envVarPath.empty()) {
      std::stringstream filePathsSS(envVarPath);
      Logger::info(str::format("Env[", desc.env, "]: ", filePathsSS.str()));
      std::string filePath;
      Config config;
      while(std::getline(filePathsSS, filePath, ',')) {
        Logger::info(str::format("Attempting to parse: ", filePath, "..."));
        config.merge(parseConfigFile(filePath));
      }
      return config;
    // As a last resort, look in the CWD for the conf file
    } else {
      Logger::info(str::format("Attempting to parse: ", desc.confName,
                               " at CWD(", std::filesystem::current_path(), ")..."));
      return parseConfigFile(desc.confName);
    }
  }
  template Config Config::getConfig<Config::Type_User>(const std::string& adtlPath);
  template Config Config::getConfig<Config::Type_App>(const std::string& adtlPath);
  template Config Config::getConfig<Config::Type_RtxUser>(const std::string& adtlPath);
  template Config Config::getConfig<Config::Type_RtxMod>(const std::string& adtlPath);
  // NV-DXVK end 

  Config Config::getAppConfig(const std::string& appName) {
    auto appConfig = std::find_if(g_appDefaults.begin(), g_appDefaults.end(),
      [&appName] (const std::pair<const char*, Config>& pair) {
        std::regex expr(pair.first, std::regex::extended | std::regex::icase);
        return std::regex_search(appName, expr);
      });
    
    if (appConfig != g_appDefaults.end()) {
      // NV-DXVK change: Update getAppConfig logging
      Logger::info(str::format("Found app config for executable: ", appName));
      return appConfig->second;
    }

    // NV-DXVK addition: Update getAppConfig logging
    Logger::info(str::format("Did not find app config for executable: ", appName));
    return Config();
  }

  void Config::serializeCustomConfig(const Config& config, std::string filePath, std::string filterStr) {
    // Open the file if it exists
    std::ofstream stream(str::tows(filePath.c_str()).c_str());

    if (!stream)
      return;

    Logger::info(str::format("Serializing config file: ", filePath));

    for (const auto& line : config.m_options) {
      if (!filterStr.empty() && line.first.find(filterStr) != std::string::npos)
        stream << line.first << " = " << line.second << std::endl;
    }
  }

  // NV-DXVK start: Extend logOptions function
  void Config::logOptions(const char* configName) const {
    if (!m_options.empty()) {
      Logger::info(str::format(configName, " configuration:"));

      for (auto& pair : m_options)
        Logger::info(str::format("  ", pair.first, " = ", pair.second));
    }
  }
  // NV-DXVK end

  std::string Config::toLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
      [] (unsigned char c) { return (c >= 'A' && c <= 'Z') ? (c + 'a' - 'A') : c; });
    return str;
  }

}
