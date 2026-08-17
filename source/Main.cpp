#include <plugin.h>
#include <CTimer.h>
#include <CWorld.h>
#include <CPlayerPed.h>
#include <CPlayerInfo.h>
#include <CStats.h>
#include <C_PcSave.h>
#include <CCutsceneMgr.h>
#include <CTheScripts.h>
#include <CPed.h>
#include <CFont.h>
#include <CRadar.h>
#ifdef GTASA
#include <CGenericGameStorage.h>
#include <eStats.h>
#else
#include <GenericGameStorage.h>
#endif
#include <CMenuManager.h>
#include <CCamera.h>
#include <CClock.h>
#include <CMessages.h>
#include <extensions/Config.h>
#include <extensions/Screen.h>
#include <Xinput.h>
// Statically imported (not LoadLibrary'd) so the .asi has a plain, inspectable import
// table - runtime API resolution trips antivirus/Nexus heuristics. xinput9_1_0.dll
// ships with every Windows since Vista, so there is nothing to fall back to.
#pragma comment(lib, "xinput9_1_0.lib")
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace plugin;

// ============================================================================
// Configuration Constants
// ============================================================================
namespace Config {
    constexpr unsigned int AUTOSAVE_COOLDOWN_MS = 15000;
    constexpr float MISSION_BLIP_DETECTION_RANGE = 10.0f;
    constexpr float MISSION_BLIP_ROTATION_RANGE = 15.0f;
    constexpr unsigned int POST_LOAD_GRACE_PERIOD_MS = 500;
    constexpr unsigned int AUTOSAVE_DISPLAY_DURATION_MS = 3000;
    constexpr int MISSION_COMPLETE_SAVE_SLOT = 6;  // Autosave on mission complete
    constexpr int MISSION_RETRY_SAVE_SLOT = 7;     // Autosave near mission marker for retry

    // Controller (see the ControllerInput namespace for accepted button names)
    constexpr const char* DEFAULT_CONTROLLER_YES = "DPadRight";
    constexpr const char* DEFAULT_CONTROLLER_NO = "DPadLeft";
    constexpr unsigned int CONTROLLER_RECONNECT_INTERVAL_MS = 2000;  // XInputGetState is slow with no pad attached
    constexpr short CONTROLLER_STICK_DEADZONE = 12000;
}

// ============================================================================
// Load Diagnostics
// ============================================================================
// A plugin that declines to install its hooks is indistinguishable from one the
// ASI loader never loaded at all, which makes "it didn't work" impossible to act
// on. Every launch therefore drops a short log next to the game exe recording the
// detected game version, the bytes the SDK fingerprinted it from, and whether each
// event actually fired. No log file at all means the .asi was never loaded.
namespace Diag {

    const char* LogPath() {
        return GAME_PATH(TARGET_NAME ".log");
    }

    void Write(const char* mode, const char* format, va_list args) {
        FILE* f = nullptr;
        if (fopen_s(&f, LogPath(), mode) != 0 || !f) return;
        vfprintf(f, format, args);
        fputc('\n', f);
        fclose(f);
    }

    void Start(const char* format, ...) {
        va_list args;
        va_start(args, format);
        Write("w", format, args);
        va_end(args);
    }

    void Line(const char* format, ...) {
        va_list args;
        va_start(args, format);
        Write("a", format, args);
        va_end(args);
    }

    // Logged once per event so a mod that loads but never runs is distinguishable
    // from one whose hooks were written to the wrong place.
    void Once(bool& flag, const char* what) {
        if (flag) return;
        flag = true;
        Line("  first %s", what);
    }

}

#ifdef GTASA
// ============================================================================
// San Andreas Game Symbols
// ============================================================================
// plugin-sdk does not resolve San Andreas addresses at compile time. Its macro
//     GLOBAL_ADDRESS_BY_VERSION(a,b,c,d,e,f) -> plugin::by_version_dyn(a,b,c,d,e,f)
// runs at load and switches on GetGameVersion(), and the SDK only ever populated the
// 1.0 US column -- every other column is a literal 0. So on 1.0 EU the SDK's
// CTheScripts::ScriptSpace, TheCamera, the CClock members and the CGenericGameStorage
// entry points all come out null, and touching any of them takes the game down. That,
// not any genuine difference between the builds, is what crashed 1.0 EU.
//
// The addresses below are the 1.0 US ones, which tools/check_addresses.py confirms
// hold identical code and data on 1.0 EU. Binding them here keeps the mod independent
// of the SDK's version table.
namespace SAGame {
    static int& OnAMissionFlag           = *reinterpret_cast<int*>(0xA476AC);
    static char* const ScriptSpace       =  reinterpret_cast<char*>(0xA49960);
    static CCamera& Camera               = *reinterpret_cast<CCamera*>(0xB6F028);
    static unsigned short& ClockSeconds  = *reinterpret_cast<unsigned short*>(0xB70150);
    static unsigned char& ClockMinutes   = *reinterpret_cast<unsigned char*>(0xB70152);
    static unsigned char& ClockHours     = *reinterpret_cast<unsigned char*>(0xB70153);

    inline void MakeValidSaveName(int slot) {
        reinterpret_cast<void(__cdecl*)(int)>(0x5D0E90)(slot);
    }

    inline bool GenericSave(int unused) {
        return reinterpret_cast<bool(__cdecl*)(int)>(0x5D13E0)(unused);
    }

    inline bool CheckSlotDataValid(int slot, bool unused) {
        return reinterpret_cast<bool(__cdecl*)(int, bool)>(0x5D1380)(slot, unused);
    }
}
#endif

// ============================================================================
// Utility Functions
// ============================================================================
namespace Utils {

    CPlayerPed* GetPlayer() {
        return CWorld::Players[0].m_pPed;
    }

    // Reads the script-space mission flag directly rather than calling
    // CTheScripts::IsPlayerOnAMission(). On San Andreas the SDK resolves that function
    // through by_version_dyn(), so it is a null pointer on anything but 1.0 US (see
    // the SAGame namespace). The bounds check matters independently of that: the events
    // this mod hooks also run before and between game sessions, when the flag still
    // holds whatever was left in it, and indexing ScriptSpace with that walks off the
    // end. Bounding it makes every one of those frames harmless.
    bool IsOnMission() {
#if defined(GTA3) || defined(GTASA)
#ifdef GTASA
        constexpr int SCRIPT_SPACE_SIZE = 200000;              // CTheScripts::ScriptSpace[200000]
#else
        const int SCRIPT_SPACE_SIZE = (int)MAX_SCRIPT_SPACE_SIZE;  // the SDK's own bound for III
#endif
#ifdef GTASA
        int flagOffset = SAGame::OnAMissionFlag;
        char* scriptSpace = SAGame::ScriptSpace;
#else
        int flagOffset = CTheScripts::OnAMissionFlag;
        char* scriptSpace = reinterpret_cast<char*>(CTheScripts::ScriptSpace);
#endif
        if (flagOffset <= 0 || flagOffset > SCRIPT_SPACE_SIZE - (int)sizeof(int)) return false;
        return *reinterpret_cast<int*>(&scriptSpace[flagOffset]) != 0;
#else
        return CTheScripts::IsPlayerOnAMission();
#endif
    }

    bool IsCutsceneRunning() {
        return CCutsceneMgr::ms_running;
    }

    bool IsGameSafeToSave() {
        if (IsCutsceneRunning()) return false;
        if (IsOnMission()) return false;

        CPlayerPed* player = GetPlayer();
        if (!player) return false;

        ePedState state = player->m_ePedState;
        if (state == PEDSTATE_DEAD || state == PEDSTATE_DIE ||
#if defined(GTA3) || defined(GTASA)
            state == PEDSTATE_ARRESTED || state == PEDSTATE_ENTER_CAR ||
            state == PEDSTATE_EXIT_CAR || state == PEDSTATE_CARJACK ||
            state == PEDSTATE_DRIVING || state == PEDSTATE_PASSENGER) {
#elif defined(GTAVC)
            state == PEDSTATE_ENTER_CAR || state == PEDSTATE_EXIT_CAR ||
            state == PEDSTATE_CAR_JACK || state == PEDSTATE_DRIVING) {
#endif
            return false;
        }

#ifdef GTASA
        if (player->m_pVehicle && player->bInVehicle) return false;
#else
        if (player->m_pVehicle && player->m_bInVehicle) return false;
#endif

        return true;
    }

    bool IsMissionGiverSprite(int sprite) {
        switch (sprite) {
#ifdef GTA3
            case RADAR_SPRITE_ASUKA:
            case RADAR_SPRITE_CAT:      // Catalina
            case RADAR_SPRITE_DON:
            case RADAR_SPRITE_EIGHT:    // 8-Ball
            case RADAR_SPRITE_EL:       // El Burro
            case RADAR_SPRITE_ICE:      // Ice Cold
            case RADAR_SPRITE_JOEY:
            case RADAR_SPRITE_KENJI:
            case RADAR_SPRITE_LIZ:      // Misty
            case RADAR_SPRITE_LUIGI:
            case RADAR_SPRITE_RAY:
            case RADAR_SPRITE_SAL:      // Salvatore
            case RADAR_SPRITE_TONY:
#elif defined(GTAVC)
            case RADAR_SPRITE_AVERY:
            case RADAR_SPRITE_BIKER:
            case RADAR_SPRITE_CORTEZ:
            case RADAR_SPRITE_DIAZ:
            case RADAR_SPRITE_KENT:
            case RADAR_SPRITE_LAWYER:
            case RADAR_SPRITE_PHIL:
            case RADAR_SPRITE_BOATYARD:
            case RADAR_SPRITE_MALIBU_CLUB:
            case RADAR_SPRITE_FILM:
            case RADAR_SPRITE_PRINTWORKS:
            case RADAR_SPRITE_CUBANS:
            case RADAR_SPRITE_HAITIANS:
            case RADAR_SPRITE_BIKERS:
            case RADAR_SPRITE_LOVEFIST:
            case RADAR_SPRITE_SUNYARD:
#elif defined(GTASA)
            case RADAR_SPRITE_BIGSMOKE:
            case RADAR_SPRITE_CATALINAPINK:
            case RADAR_SPRITE_CESARVIAPANDO:
            case RADAR_SPRITE_CJ:
            case RADAR_SPRITE_CRASH1:
            case RADAR_SPRITE_LOGOSYNDICATE:
            case RADAR_SPRITE_MADDOG:
            case RADAR_SPRITE_MAFIACASINO:
            case RADAR_SPRITE_MCSTRAP:
            case RADAR_SPRITE_OGLOC:
            case RADAR_SPRITE_RYDER:
            case RADAR_SPRITE_QMARK:
            case RADAR_SPRITE_SWEET:
            case RADAR_SPRITE_THETRUTH:
            case RADAR_SPRITE_TORENORANCH:
            case RADAR_SPRITE_TRIADS:
            case RADAR_SPRITE_TRIADSCASINO:
            case RADAR_SPRITE_WOOZIE:
            case RADAR_SPRITE_ZERO:
#endif
                return true;
            default:
                return false;
        }
    }

    bool IsPlayerNearMissionBlip(float maxDistance) {
        CPlayerPed* player = GetPlayer();
        if (!player) return false;

        CVector playerPos = player->GetPosition();

#ifdef GTASA
        int traceCount = (int)MAX_RADAR_TRACES;
#else
        int traceCount = 32;
#endif
        for (int i = 0; i < traceCount; i++) {
            const tRadarTrace& blip = CRadar::ms_RadarTrace[i];
            if (!blip.m_bInUse) continue;

            if (IsMissionGiverSprite(blip.m_nRadarSprite)) {
                float dx = playerPos.x - blip.m_vecPos.x;
                float dy = playerPos.y - blip.m_vecPos.y;
                float distSq = dx * dx + dy * dy;

                if (distSq < maxDistance * maxDistance) {
                    return true;
                }
            }
        }
        return false;
    }

    bool FindNearestMissionBlip(float maxDistance, CVector& outBlipPos) {
        CPlayerPed* player = GetPlayer();
        if (!player) return false;

        CVector playerPos = player->GetPosition();
        float nearestDistSq = maxDistance * maxDistance;
        bool found = false;

#ifdef GTASA
        int traceCount = (int)MAX_RADAR_TRACES;
#else
        int traceCount = 32;
#endif
        for (int i = 0; i < traceCount; i++) {
            const tRadarTrace& blip = CRadar::ms_RadarTrace[i];
            if (!blip.m_bInUse) continue;

            if (IsMissionGiverSprite(blip.m_nRadarSprite)) {
                float dx = playerPos.x - blip.m_vecPos.x;
                float dy = playerPos.y - blip.m_vecPos.y;
                float distSq = dx * dx + dy * dy;

                if (distSq < nearestDistSq) {
                    nearestDistSq = distSq;
                    outBlipPos = blip.m_vecPos;
                    found = true;
                }
            }
        }
        return found;
    }

    float CalculateHeadingToTarget(const CVector& from, const CVector& to) {
        float dx = to.x - from.x;
        float dy = to.y - from.y;
        return atan2(dx, dy) - 1.5707963f;
    }

    void SetPlayerAndCameraHeading(CPlayerPed* player, float heading) {
        if (!player) return;

#ifdef GTASA
        CCamera& camera = SAGame::Camera;
#else
        CCamera& camera = TheCamera;
#endif
        int activeCam = camera.m_nActiveCam;
#ifdef GTASA
        if (activeCam < 0 || activeCam >= (int)(sizeof(camera.m_aCams) / sizeof(camera.m_aCams[0]))) return;
        camera.m_aCams[activeCam].m_fHorizontalAngle = heading;
        camera.m_aCams[activeCam].m_fTargetBeta = heading;
        camera.m_aCams[activeCam].m_fTrueBeta = heading;
        camera.m_aCams[activeCam].m_fTransitionBeta = heading;
#else
        camera.m_asCams[activeCam].m_fHorizontalAngle = heading;
        camera.m_asCams[activeCam].m_fTargetBeta = heading;
        camera.m_asCams[activeCam].m_fTrueBeta = heading;
        camera.m_asCams[activeCam].m_fTransitionBeta = heading;
#endif
#ifdef GTASA
        player->m_fCurrentRotation = heading;
        player->m_fAimingRotation = heading;
#else
        player->m_fRotationCur = heading;
        player->m_fRotationDest = heading;
#endif
    }

    bool IsMissionFailedTextVisible() {
#ifdef GTA3
        if (!CMessages::BIGMessages) return false;

        unsigned int currentTime = CTimer::m_snTimeInMilliseconds;

        // Check all 6 big message slots
        for (int i = 0; i < 6; i++) {
            const tBigMessage& bigMsg = CMessages::BIGMessages[i];
            const tMessage& msg = bigMsg.m_Current;

            // Check if message is currently active (time hasn't expired)
            if (msg.m_pText && msg.m_nTime > 0) {
                if (currentTime < msg.m_nStartTime + msg.m_nTime) {
                    // Check if text contains "MISSION FAILED" or "M_FAIL" (common GXT key)
                    if (msg.m_pText) {
                        // Convert to lowercase for comparison
                        wchar_t lower[128] = {0};
                        for (int j = 0; j < 127 && msg.m_pText[j]; j++) {
                            lower[j] = (msg.m_pText[j] >= 'A' && msg.m_pText[j] <= 'Z')
                                      ? (msg.m_pText[j] + 32)
                                      : msg.m_pText[j];
                        }

                        // Check for mission failed indicators
                        if (wcsstr(lower, L"mission failed") != nullptr ||
                            wcsstr(lower, L"m_fail") != nullptr ||
                            wcsstr(msg.m_pText, L"M_FAIL") != nullptr) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
#elif defined(GTASA)
        unsigned int currentTime = CTimer::m_snTimeInMilliseconds;

        for (int i = 0; i < (int)eMessageStyle::STYLE_COUNT; i++) {
            const tBigMessage& bigMsg = CMessages::BIGMessages[i];
            const tMessage& msg = bigMsg.m_Current;

            if (msg.m_pText && msg.m_dwTime > 0) {
                if (currentTime < msg.m_dwStartTime + msg.m_dwTime) {
                    if (msg.m_pText) {
                        char lower[128] = {0};
                        for (int j = 0; j < 127 && msg.m_pText[j]; j++) {
                            lower[j] = (msg.m_pText[j] >= 'A' && msg.m_pText[j] <= 'Z')
                                      ? (msg.m_pText[j] + 32)
                                      : msg.m_pText[j];
                        }

                        if (strstr(lower, "mission failed") != nullptr ||
                            strstr(lower, "m_fail") != nullptr ||
                            strstr(msg.m_pText, "M_FAIL") != nullptr) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
#elif defined(GTAVC)
        // Vice City doesn't expose the BIGMessages array, so we use an alternative approach:
        // Detect mission failure by tracking when player goes from on-mission to off-mission
        // without the mission count increasing. This is handled in HandleMissionRetry().
        return false;
#endif
    }

    void DrawText(float x, float y, const char* text, float scaleX, float scaleY, CRGBA color) {
#ifdef GTASA
        CFont::SetOrientation(ALIGN_LEFT);
        CFont::SetBackground(false, false);
        CFont::SetScale(scaleX, scaleY);
        CFont::SetFontStyle(FONT_GOTHIC);
        CFont::SetProportional(true);
        CFont::SetWrapx(500.0f);
        CFont::SetColor(color);
        CFont::SetDropShadowPosition(2);
        CFont::SetDropColor(CRGBA(0, 0, 0, 255));
        CFont::PrintString(x, y, text);
#else
        CFont::SetJustifyOff();
        CFont::SetRightJustifyOff();
        CFont::SetBackgroundOff();
        CFont::SetScale(scaleX, scaleY);
        CFont::SetFontStyle(FONT_HEADING);
        CFont::SetPropOn();
        CFont::SetWrapx(500.0f);
        CFont::SetColor(color);
        CFont::SetDropShadowPosition(2);
        CFont::SetDropColor(CRGBA(0, 0, 0, 255));
        wchar_t wtext[256];
        AsciiToUnicode(text, wtext);
        CFont::PrintString(x, y, wtext);
#endif
    }

    void DrawCenteredText(float x, float y, const char* text, float scaleX, float scaleY, CRGBA color) {
#ifdef GTASA
        CFont::SetOrientation(ALIGN_CENTER);
        CFont::SetCentreSize(500.0f);
        DrawText(x, y, text, scaleX, scaleY, color);
        CFont::SetOrientation(ALIGN_LEFT);
#else
        CFont::SetCentreOn();
        CFont::SetCentreSize(500.0f);
        DrawText(x, y, text, scaleX, scaleY, color);
        CFont::SetCentreOff();
#endif
    }

} // namespace Utils

// ============================================================================
// Controller Input - XInput polling
// ============================================================================
// The stock III/VC executables fold a joypad's D-Pad into the left stick and only
// write CControllerState's DPad* fields from bound keyboard actions, so CPad never
// sees a real controller's D-Pad. Polling XInput directly bypasses the game's input
// plumbing and gives one code path for all three games.
namespace ControllerInput {

    struct ButtonName {
        const char* name;    // as written in the ini
        const char* label;   // as shown in the retry prompt
        WORD mask;
    };

    static const ButtonName BUTTON_NAMES[] = {
        { "None",           "",              0                              },
        { "DPadUp",         "D-Pad Up",      XINPUT_GAMEPAD_DPAD_UP         },
        { "DPadDown",       "D-Pad Down",    XINPUT_GAMEPAD_DPAD_DOWN       },
        { "DPadLeft",       "D-Pad Left",    XINPUT_GAMEPAD_DPAD_LEFT       },
        { "DPadRight",      "D-Pad Right",   XINPUT_GAMEPAD_DPAD_RIGHT      },
        { "Start",          "Start",         XINPUT_GAMEPAD_START           },
        { "Back",           "Back",          XINPUT_GAMEPAD_BACK            },
        { "LeftThumb",      "L3",            XINPUT_GAMEPAD_LEFT_THUMB      },
        { "RightThumb",     "R3",            XINPUT_GAMEPAD_RIGHT_THUMB     },
        { "LeftShoulder",   "LB",            XINPUT_GAMEPAD_LEFT_SHOULDER   },
        { "RightShoulder",  "RB",            XINPUT_GAMEPAD_RIGHT_SHOULDER  },
        { "A",              "A",             XINPUT_GAMEPAD_A               },
        { "B",              "B",             XINPUT_GAMEPAD_B               },
        { "X",              "X",             XINPUT_GAMEPAD_X               },
        { "Y",              "Y",             XINPUT_GAMEPAD_Y               },
    };

    static bool s_connected = false;
    static bool s_everUsed = false;
    static WORD s_currButtons = 0;
    static WORD s_prevButtons = 0;
    static unsigned int s_lastProbeTime = 0;
    static bool s_probed = false;

    void Update(unsigned int currentTime) {
        s_prevButtons = s_currButtons;

        // Back off between probes while nothing is plugged in.
        // CTimer resets on load, so a backwards jump forces an immediate re-probe.
        if (!s_connected && s_probed &&
            currentTime >= s_lastProbeTime &&
            currentTime - s_lastProbeTime < Config::CONTROLLER_RECONNECT_INTERVAL_MS) {
            s_currButtons = 0;
            return;
        }

        s_lastProbeTime = currentTime;
        s_probed = true;

        XINPUT_STATE state = {};
        if (XInputGetState(0, &state) != ERROR_SUCCESS) {
            s_connected = false;
            s_currButtons = 0;
            return;
        }

        s_connected = true;
        s_currButtons = state.Gamepad.wButtons;

        if (!s_everUsed) {
            const XINPUT_GAMEPAD& pad = state.Gamepad;
            constexpr short DZ = Config::CONTROLLER_STICK_DEADZONE;
            if (pad.wButtons != 0 || pad.bLeftTrigger > 64 || pad.bRightTrigger > 64 ||
                abs(pad.sThumbLX) > DZ || abs(pad.sThumbLY) > DZ ||
                abs(pad.sThumbRX) > DZ || abs(pad.sThumbRY) > DZ) {
                s_everUsed = true;
            }
        }
    }

    bool IsButtonJustDown(WORD mask) {
        return mask != 0 && (s_currButtons & mask) != 0 && (s_prevButtons & mask) == 0;
    }

    bool IsConnected() { return s_connected; }

    // True once the player has actually touched the pad - drives the prompt wording
    bool WasEverUsed() { return s_everUsed; }

    WORD GetButtonState() { return s_currButtons; }

    // Resolves an ini button name. Returns false (leaving outputs untouched) if unknown.
    bool ParseButtonName(const char* name, WORD& outMask, const char*& outLabel) {
        for (const ButtonName& entry : BUTTON_NAMES) {
            if (_stricmp(name, entry.name) == 0) {
                outMask = entry.mask;
                outLabel = entry.label;
                return true;
            }
        }
        return false;
    }

} // namespace ControllerInput

// ============================================================================
// AutosaveMod Class - Main mod logic
// ============================================================================
class AutosaveMod {
public:
    AutosaveMod() {
        // Instance() constructs a second AutosaveMod, and that one must not register
        // another set of handlers -- doing so both doubles every per-frame action and
        // mutates the handler list while the event that triggered it is still walking
        // it. Only the first construction installs anything.
        static bool installed = false;
        if (installed) return;
        installed = true;

        LogEnvironment();

        if (!IsUsableGameVersion()) {
            Diag::Line("  refused: game version not supported");
            Error("Unsupported game version: %s\n\n"
                  "This mod only works with:\n    %s\n\n"
                  "Downgrade the game to that version, or remove this mod.",
                  GetGameVersionName(), SupportedVersionsText().c_str());
            return;
        }

#ifdef GTASA
        if (!InstallHooks()) return;
#else
        Events::initGameEvent += []{ RunGuarded(&AutosaveMod::OnGameInit, "OnGameInit"); };
        Events::gameProcessEvent += []{ RunGuarded(&AutosaveMod::OnGameProcess, "OnGameProcess"); };
        Events::drawHudEvent += []{ RunGuarded(&AutosaveMod::OnDrawHud, "OnDrawHud"); };
#endif

        Diag::Line("  hooks installed");
    }

private:
    // Hooks are only installed on a build this mod has actually been checked against;
    // anywhere else the addresses it patches and reads would land in unrelated code.
    //
    // San Andreas 1.0 EU is accepted even though plugin-sdk only claims 1.0 US. The two
    // images are the same build with two displaced code regions -- identical below
    // 0x741000, +0x50 through 0x7C0FFF, +0x40 for the rest of .text -- and every global
    // and function this mod uses lives in the identical region. Supporting it took two
    // things the SDK gets wrong off 1.0 US, both handled elsewhere in this file: its
    // events never install (see InstallHooks) and its version-dispatched addresses all
    // resolve to null (see the SAGame namespace). Nothing here depends on an address
    // above 0x741000, which is the one region that genuinely moved.
    //
    // tools/check_addresses.py reproduces the comparison against a pair of exes.
    static bool IsUsableGameVersion() {
#ifdef GTASA
        if (GetGameVersion() == GAME_10EU) return true;
#endif
        return IsSupportedGameVersion();
    }

#ifdef GTASA
    // plugin::Events cannot be used on the SA target. For San Andreas
    // AddressList<Addr, H_CALL> expands to two RefList entries tagged
    // GAME_10US_COMPACT and GAME_10US_HOODLUM, and EventList's PatchAll installs a
    // hook only where its tag equals GetGameVersion(). On 1.0 EU that is GAME_10EU,
    // so `event += handler` matched nothing, patched nothing, and reported nothing --
    // the mod loaded and sat inert. Both call sites hold identical bytes across all
    // three accepted builds (see IsUsableGameVersion), so patch them directly.
    using GameHookFn = void (__cdecl *)();
    static inline GameHookFn s_originalGameProcess = nullptr;
    static inline GameHookFn s_originalDrawHud = nullptr;

    static void __cdecl GameProcessHook() {
        s_originalGameProcess();
        RunGuarded(&AutosaveMod::OnGameProcess, "OnGameProcess");
    }

    static void __cdecl DrawHudHook() {
        s_originalDrawHud();
        RunGuarded(&AutosaveMod::OnDrawHud, "OnDrawHud");
    }

    // Bypassing the SDK's version gate means bypassing its safety, so refuse to write
    // anywhere that does not already hold the CALL we expect to be replacing.
    static bool PatchCall(unsigned int address, void* hook, GameHookFn& original) {
        unsigned char opcode = *reinterpret_cast<unsigned char*>(address);
        if (opcode != 0xE8) {
            Diag::Line("  refused: %#010x holds %#04x, expected a call (0xE8)", address, opcode);
            return false;
        }
        original = reinterpret_cast<GameHookFn>(injector::MakeCALL(address, hook).get<void>());
        Diag::Line("  patched %#010x -> original %p", address, reinterpret_cast<void*>(original));
        return original != nullptr;
    }

    static bool InstallHooks() {
        return PatchCall(0x53E981, GameProcessHook, s_originalGameProcess)
            && PatchCall(0x53E4FF, DrawHudHook, s_originalDrawHud);
    }
#endif

    // A mod has no business taking the game down with it. Every entry point runs behind
    // this: a fault is written to the log with the faulting address and the module it
    // landed in, and the mod then switches itself off for the rest of the session rather
    // than faulting again on the very next frame.
    static inline bool s_faulted = false;

    static int ReportFault(EXCEPTION_POINTERS* info, const char* where) {
        s_faulted = true;

        void* at = info->ExceptionRecord->ExceptionAddress;
        HMODULE module = nullptr;
        const char* name = "?";
        char path[MAX_PATH] = "";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(at), &module) && module &&
            GetModuleFileNameA(module, path, MAX_PATH)) {
            const char* slash = strrchr(path, '\\');
            name = slash ? slash + 1 : path;
        }
        Diag::Line("  FAULT in %s: code %#010x at %p (%s+%#x) -- mod disabled for this session",
                   where, info->ExceptionRecord->ExceptionCode, at, name,
                   static_cast<unsigned int>(static_cast<char*>(at) - reinterpret_cast<char*>(module)));
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // No locals with destructors here -- MSVC rejects __try in a function that needs
    // unwinding, which is why this is a wrapper rather than a guard inside each handler.
    static void RunGuarded(void (AutosaveMod::*handler)(), const char* where) {
        if (s_faulted) return;
        __try {
            (Instance().*handler)();
        }
        __except (ReportFault(GetExceptionInformation(), where)) {
        }
    }

    // Records what the SDK's fingerprint actually saw at runtime. On a protected or
    // repacked exe the bytes in memory need not match the ones on disk, so this is
    // the only trustworthy reading of which build the mod believes it is attached to.
    static void LogEnvironment() {
        Diag::Start("%s -- built %s %s", TARGET_NAME, __DATE__, __TIME__);
        Diag::Line("  game version   %s (id %u)", GetGameVersionName(), GetGameVersion());
        Diag::Line("  supported      %s", SupportedVersionsText().c_str());
#ifdef GTASA
        Diag::Line("  [0x401000]     %#010x   [0x8245BC] %#06x",
                   plugin::patch::GetUInt(0x401000), plugin::patch::GetUInt(0x8245BC));
        Diag::Line("  [0x53E981]     %#010x   [0x53E4FF] %#010x  (hook sites, expect E8...)",
                   plugin::patch::GetUInt(0x53E981), plugin::patch::GetUInt(0x53E4FF));
#endif
    }

    static std::string SupportedVersionsText() {
        std::string text = GetSupportedGameVersionsString("\n    ");
#ifdef GTASA
        text += "\n    ";
        text += GetGameVersionName(GAME_10EU);
#endif
        return text;
    }

    // Singleton access for event callbacks
    static AutosaveMod& Instance() {
        static AutosaveMod instance;
        return instance;
    }

    // ========================================================================
    // Configuration
    // ========================================================================
    struct Settings {
        bool debugMode = false;
        bool approachAutosaveEnabled = true;
        bool missionCompleteAutosaveEnabled = true;
        WORD controllerYesButton = XINPUT_GAMEPAD_DPAD_RIGHT;
        WORD controllerNoButton = XINPUT_GAMEPAD_DPAD_LEFT;
        const char* controllerYesLabel = "D-Pad Right";
        const char* controllerNoLabel = "D-Pad Left";
    } m_settings;

    // ========================================================================
    // State tracking
    // ========================================================================
    
#ifdef GTASA
    bool m_initialised = false;  // Guards the first-frame setup OnGameProcess does
#endif

    // One-shot markers for the load log
    bool m_loggedInit = false;
    bool m_loggedProcess = false;
    bool m_loggedDrawHud = false;

    // Load detection
    bool m_justLoaded = false;
    unsigned int m_loadedAtTime = 0;
    unsigned int m_lastGameTime = 0;

    // Autosave state
    bool m_wasNearMissionBlip = false;
    unsigned int m_lastNearBlipAutosaveTime = 0;  // Independent cooldown for near-marker saves
    unsigned int m_lastMissionCompleteAutosaveTime = 0;  // Independent cooldown for mission complete saves
    bool m_pendingAutosave = false;
    bool m_pendingMissionCompleteSave = false;
    unsigned int m_autosaveDisplayUntil = 0;  // Shared display timer (only one notification at a time)

    // Mission retry state
    int m_lastMissionsPassed = -1;
    bool m_wasOnMission = false;  // Track previous mission state to detect new mission start
    bool m_wasMissionFailedTextVisible = false;  // Track if mission failed text was visible last frame
    bool m_showRetryPrompt = false;
    bool m_retryYKeyWasPressed = false;
    bool m_retryNKeyWasPressed = false;
    char m_retryPromptText[96] = "";

    // Debug
    char m_debugText[256] = "";
    char m_saveDebugText[256] = "";
    unsigned int m_saveDebugDisplayUntil = 0;

    // ========================================================================
    // Event Handlers
    // ========================================================================
    
    void OnGameInit() {
        Diag::Once(m_loggedInit, "init");
        LoadConfig();
        ResetLoadState();
        m_autosaveDisplayUntil = 0;
    }

    void OnGameProcess() {
        Diag::Once(m_loggedProcess, "process tick");

        // This event also runs on the menu, where there is no world yet: the mission
        // flag CTheScripts::IsPlayerOnAMission() indexes ScriptSpace with is still
        // uninitialised there, so calling it walks off into unmapped memory and takes
        // the process down. Nothing below has anything to do without a player anyway.
        if (!Utils::GetPlayer()) return;

#ifdef GTASA
        // SA does not hook initGameEvent, so the one-time setup happens here on the
        // first processed frame. Unlike that event this does not fire again when the
        // player restarts, but nothing in OnGameInit needs to: the config and the pad
        // only have to be read once, and DetectGameLoad already treats the CTimer
        // reset a restart causes as a load.
        if (!m_initialised) {
            m_initialised = true;
            OnGameInit();
        }
#endif
        unsigned int currentTime = CTimer::m_snTimeInMilliseconds;

        ControllerInput::Update(currentTime);
        DetectGameLoad(currentTime);
        HandlePostLoadState(currentTime);
        HandleAutosave(currentTime);
        HandleMissionRetry(currentTime);
        UpdateDebugInfo(currentTime);

        m_lastGameTime = currentTime;
    }

    void OnDrawHud() {
        Diag::Once(m_loggedDrawHud, "hud draw");
        DrawDebugInfo();
        DrawAutosaveNotification();
        DrawRetryPrompt();
    }

    // ========================================================================
    // Configuration Management
    // ========================================================================
    
    void LoadConfig() {
        config_file config(true, false);
        m_settings.debugMode = config["Debug"].asInt(0) != 0;
        m_settings.approachAutosaveEnabled = config["ApproachAutosave"].asInt(1) != 0;
        m_settings.missionCompleteAutosaveEnabled = config["MissionCompleteAutosave"].asInt(1) != 0;

        // Unknown button names fall back to the defaults already in m_settings
        std::string yesButton = config["ControllerRetryYes"].asString(Config::DEFAULT_CONTROLLER_YES);
        ControllerInput::ParseButtonName(yesButton.c_str(), m_settings.controllerYesButton, m_settings.controllerYesLabel);
        std::string noButton = config["ControllerRetryNo"].asString(Config::DEFAULT_CONTROLLER_NO);
        ControllerInput::ParseButtonName(noButton.c_str(), m_settings.controllerNoButton, m_settings.controllerNoLabel);

        bool needSave = false;
        if (config["Debug"].isEmpty()) {
            config["Debug"] = 0;
            needSave = true;
        }
        if (config["ApproachAutosave"].isEmpty()) {
            config["ApproachAutosave"] = 1;
            needSave = true;
        }
        if (config["MissionCompleteAutosave"].isEmpty()) {
            config["MissionCompleteAutosave"] = 1;
            needSave = true;
        }
        if (config["ControllerRetryYes"].isEmpty()) {
            config["ControllerRetryYes"] = Config::DEFAULT_CONTROLLER_YES;
            needSave = true;
        }
        if (config["ControllerRetryNo"].isEmpty()) {
            config["ControllerRetryNo"] = Config::DEFAULT_CONTROLLER_NO;
            needSave = true;
        }
        if (needSave) {
            config.save();
        }
    }

    // ========================================================================
    // Load Detection & State Reset
    // ========================================================================
    
    void ResetLoadState() {
        m_justLoaded = true;
        m_loadedAtTime = 0;
    }

    void DetectGameLoad(unsigned int currentTime) {
        if (m_lastGameTime > 0 && currentTime < m_lastGameTime) {
            ResetLoadState();
            m_autosaveDisplayUntil = 0;
        }
    }

    // ========================================================================
    // Post-Load Handling (Rotation & Autosave Prevention)
    // ========================================================================
    
    void HandlePostLoadState(unsigned int currentTime) {
        if (!m_justLoaded) return;

        bool isNearBlip = !Utils::IsOnMission() && 
                          !Utils::IsCutsceneRunning() && 
                          Utils::IsPlayerNearMissionBlip(Config::MISSION_BLIP_DETECTION_RANGE);

        if (m_loadedAtTime == 0) {
            m_loadedAtTime = currentTime;

            // Prevent immediate autosave
            m_wasNearMissionBlip = isNearBlip;
            m_lastNearBlipAutosaveTime = isNearBlip ? currentTime : 0;

            // Rotate player to face nearest mission blip
            RotatePlayerToNearestBlip();
        }

        if (currentTime > m_loadedAtTime + Config::POST_LOAD_GRACE_PERIOD_MS) {
            m_justLoaded = false;
        }
    }

    void RotatePlayerToNearestBlip() {
        CPlayerPed* player = Utils::GetPlayer();
        if (!player) return;

        CVector blipPos;
        if (Utils::FindNearestMissionBlip(Config::MISSION_BLIP_ROTATION_RANGE, blipPos)) {
            CVector playerPos = player->GetPosition();
            float heading = Utils::CalculateHeadingToTarget(playerPos, blipPos);
            Utils::SetPlayerAndCameraHeading(player, heading);
        }
    }

    // ========================================================================
    // Autosave Feature
    // ========================================================================
    
    void HandleAutosave(unsigned int currentTime) {
        if (!m_settings.approachAutosaveEnabled) return;

        bool isOnMission = Utils::IsOnMission();
        bool isNearBlip = !isOnMission &&
                          !Utils::IsCutsceneRunning() &&
                          Utils::IsPlayerNearMissionBlip(Config::MISSION_BLIP_DETECTION_RANGE);

        // Trigger pending save when entering blip area (with cooldown)
        if (!m_justLoaded && isNearBlip && !m_wasNearMissionBlip) {
            if (currentTime > m_lastNearBlipAutosaveTime + Config::AUTOSAVE_COOLDOWN_MS) {
                m_pendingAutosave = true;
                // Paired with the result line in PerformAutosave: a pending line with no
                // result after it means IsGameSafeToSave() kept refusing -- being in a
                // vehicle is the usual reason, and is deliberate.
                Diag::Line("  mission marker in range, autosave pending");
            }
        }

        if (!m_justLoaded) {
            m_wasNearMissionBlip = isNearBlip;
        }

        // Cancel if mission starts
        if (isOnMission) {
            m_pendingAutosave = false;
        }

        // Execute autosave when safe
        if (m_pendingAutosave && Utils::IsGameSafeToSave()) {
            if (PerformAutosave(currentTime, Config::MISSION_RETRY_SAVE_SLOT)) {
                m_pendingAutosave = false;  // Only clear pending flag if save succeeded
            }
            // If save failed, keep m_pendingAutosave true to retry on next frame
        }
    }

    bool PerformAutosave(unsigned int currentTime, int slot) {
        // Preserve game time (saving normally advances clock by 6 hours)
#ifdef GTASA
        unsigned char savedHours = SAGame::ClockHours;
        unsigned char savedMinutes = SAGame::ClockMinutes;
        unsigned short savedSeconds = SAGame::ClockSeconds;
#else
        unsigned char savedHours = CClock::ms_nGameClockHours;
        unsigned char savedMinutes = CClock::ms_nGameClockMinutes;
        unsigned short savedSeconds = CClock::ms_nGameClockSeconds;
#endif

        // Attempt to save - check return value
#ifdef GTASA
        SAGame::MakeValidSaveName(slot);
        bool saveSuccess = SAGame::GenericSave(0);
#else
        bool saveSuccess = PcSaveHelper.SaveSlot(slot);
#endif

        // Restore game time
#ifdef GTASA
        SAGame::ClockHours = savedHours;
        SAGame::ClockMinutes = savedMinutes;
        SAGame::ClockSeconds = savedSeconds;
#else
        CClock::ms_nGameClockHours = savedHours;
        CClock::ms_nGameClockMinutes = savedMinutes;
        CClock::ms_nGameClockSeconds = savedSeconds;
#endif

        Diag::Line("  autosave slot %d: %s", slot, saveSuccess ? "ok" : "reported failure");

        // Vice City: SaveSlot() returns false even when save succeeds, so always show notification
        // GTA III and SA: Use actual return value
#ifdef GTAVC
        bool shouldShowNotification = true;
#else
        bool shouldShowNotification = saveSuccess;
#endif

        if (shouldShowNotification) {
            // Update the appropriate cooldown timer based on slot
            if (slot == Config::MISSION_COMPLETE_SAVE_SLOT) {
                m_lastMissionCompleteAutosaveTime = currentTime;
            } else if (slot == Config::MISSION_RETRY_SAVE_SLOT) {
                m_lastNearBlipAutosaveTime = currentTime;
            }

            // Always update display timer
            m_autosaveDisplayUntil = currentTime + Config::AUTOSAVE_DISPLAY_DURATION_MS;

            // Debug: log the save
            if (m_settings.debugMode) {
                sprintf_s(m_saveDebugText, sizeof(m_saveDebugText), "SAVED! until=%u (now=%u)",
                         m_autosaveDisplayUntil, currentTime);
                m_saveDebugDisplayUntil = currentTime + 2000;
            }
        }

#ifdef GTAVC
        return shouldShowNotification;
#else
        return saveSuccess;
#endif
    }

    // ========================================================================
    // Mission Retry Feature
    // ========================================================================
    
    void HandleMissionRetry(unsigned int currentTime) {
#ifdef GTASA
        int missionsPassed = (int)CStats::GetStatValue(STAT_MISSIONS_PASSED);
#else
        int missionsPassed = CStats::MissionsPassed;
#endif
        bool isOnMission = Utils::IsOnMission();

        // Reset on load/new game
        if (m_justLoaded || m_lastMissionsPassed == -1 || missionsPassed < m_lastMissionsPassed) {
            ResetMissionRetryState(missionsPassed);
        }

        // Clear prompt if a new mission starts while it's visible
        if (m_showRetryPrompt && isOnMission && !m_wasOnMission) {
            m_showRetryPrompt = false;
        }

        // Primary detection: Check if "Mission Failed" text is visible on screen
        bool missionFailedTextVisible = Utils::IsMissionFailedTextVisible();

#if defined(GTA3) || defined(GTASA)
        // GTA III/SA: Show prompt whenever mission failed text appears (simple and reliable)
        if (missionFailedTextVisible && !m_wasMissionFailedTextVisible) {
            // Mission failed text just appeared - show retry prompt if we have a save
#ifdef GTASA
            if (SAGame::CheckSlotDataValid(Config::MISSION_RETRY_SAVE_SLOT, false)) {
#else
            if (CheckSlotDataValid(Config::MISSION_RETRY_SAVE_SLOT)) {
#endif
                m_showRetryPrompt = true;
            }
        }
#elif defined(GTAVC)
        // Vice City: Detect mission failure by tracking mission state changes
        // If player was on mission, is now off mission, and mission count didn't increase -> mission failed
        if (m_wasOnMission && !isOnMission && missionsPassed == m_lastMissionsPassed) {
            // Mission ended without success - show retry prompt if we have a save
            if (CheckSlotDataValid(Config::MISSION_RETRY_SAVE_SLOT)) {
                m_showRetryPrompt = true;
            }
        }
#endif

        // Track mission failed text visibility (prompt stays visible until user interacts)
        m_wasMissionFailedTextVisible = missionFailedTextVisible;
        m_wasOnMission = isOnMission;

        // Autosave on mission success
        if (missionsPassed > m_lastMissionsPassed) {
            m_lastMissionsPassed = missionsPassed;
            m_showRetryPrompt = false;
            if (m_settings.missionCompleteAutosaveEnabled) {
                m_pendingMissionCompleteSave = true;
            }
        }

        // Execute mission complete autosave when safe
        if (m_pendingMissionCompleteSave && Utils::IsGameSafeToSave()) {
            if (PerformAutosave(currentTime, Config::MISSION_COMPLETE_SAVE_SLOT)) {
                m_pendingMissionCompleteSave = false;  // Only clear pending flag if save succeeded
            }
            // If save failed, keep m_pendingMissionCompleteSave true to retry on next frame
        }

        // Handle retry input (called every frame so the key latches stay fresh - a key
        // already held down when the prompt appears must not answer it instantly)
        HandleRetryInput();
    }

    void ResetMissionRetryState(int missionsPassed) {
        m_lastMissionsPassed = missionsPassed;
        m_wasOnMission = false;
        m_wasMissionFailedTextVisible = false;
        m_showRetryPrompt = false;
    }

    void HandleRetryInput() {
        bool yPressed = KeyPressed('Y');
        bool nPressed = KeyPressed('N');

        // ControllerInput does its own edge detection every frame, so no latch needed here
        bool yesJustPressed = (yPressed && !m_retryYKeyWasPressed) ||
                              ControllerInput::IsButtonJustDown(m_settings.controllerYesButton);
        bool noJustPressed = (nPressed && !m_retryNKeyWasPressed) ||
                             ControllerInput::IsButtonJustDown(m_settings.controllerNoButton);

        m_retryYKeyWasPressed = yPressed;
        m_retryNKeyWasPressed = nPressed;

        if (!m_showRetryPrompt) return;

        if (yesJustPressed) {
            LoadAutosave();
            m_showRetryPrompt = false;
        }
        else if (noJustPressed) {
            m_showRetryPrompt = false;
        }
    }

    void LoadAutosave() {
#ifdef GTA3
        MakeValidSaveName(Config::MISSION_RETRY_SAVE_SLOT);
        FrontEndMenuManager.m_nCurrentSaveSlot = Config::MISSION_RETRY_SAVE_SLOT;
#elif defined(GTASA)
        SAGame::MakeValidSaveName(Config::MISSION_RETRY_SAVE_SLOT);
        FrontEndMenuManager.m_nSelectedSaveGame = Config::MISSION_RETRY_SAVE_SLOT;
#elif defined(GTAVC)
        FrontEndMenuManager.m_nCurrentSaveSlot = Config::MISSION_RETRY_SAVE_SLOT;
#endif
        FrontEndMenuManager.m_bWantToLoad = true;
        FrontEndMenuManager.m_bWantToRestart = true;
#ifndef GTASA
        b_FoundRecentSavedGameWantToLoad = true;
#endif
    }

    // ========================================================================
    // Debug & HUD Drawing
    // ========================================================================
    
    void UpdateDebugInfo(unsigned int currentTime) {
        if (!m_settings.debugMode) return;

        bool nearBlip = Utils::IsPlayerNearMissionBlip(Config::MISSION_BLIP_DETECTION_RANGE);
#ifdef GTASA
        int missionsPassed = (int)CStats::GetStatValue(STAT_MISSIONS_PASSED);
#else
        int missionsPassed = CStats::MissionsPassed;
#endif
        bool isOnMission = Utils::IsOnMission();
        bool missionFailedVisible = Utils::IsMissionFailedTextVisible();

        sprintf_s(m_debugText, sizeof(m_debugText), "near=%d load=%d miss=%d onmiss=%d failtxt=%d pad=%d btn=%04X",
            nearBlip, m_justLoaded, missionsPassed, isOnMission, missionFailedVisible,
            ControllerInput::IsConnected(), ControllerInput::GetButtonState());
    }

    void DrawDebugInfo() {
        if (!m_settings.debugMode) return;

        // Draw regular debug info
        if (m_debugText[0] != '\0') {
            CFont::SetDropShadowPosition(1);
            Utils::DrawText(30.0f, 30.0f, m_debugText, 0.4f, 0.8f, CRGBA(255, 200, 100, 255));
        }

        // Draw save debug info (on second line, with timer)
        unsigned int currentTime = CTimer::m_snTimeInMilliseconds;
        if (currentTime < m_saveDebugDisplayUntil && m_saveDebugText[0] != '\0') {
            CFont::SetDropShadowPosition(1);
            Utils::DrawText(30.0f, 55.0f, m_saveDebugText, 0.4f, 0.8f, CRGBA(255, 100, 255, 255));
        }
    }

    void DrawAutosaveNotification() {
        unsigned int currentTime = static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds);
        bool shouldShow = (currentTime < m_autosaveDisplayUntil);

#ifdef GTA3
        // GTA III: Position at bottom-left (minimap is at top-left)
        float x = SCREEN_COORD_LEFT(30.0f);
        float y = SCREEN_COORD_BOTTOM(60.0f);
#elif defined(GTAVC) || defined(GTASA)
        // Vice City / SA: Position at bottom-left, well above minimap
        float x = SCREEN_COORD_LEFT(30.0f);
        float y = SCREEN_COORD_BOTTOM(350.0f);
#endif

        // Debug: show detailed diagnostic info
        if (m_settings.debugMode) {
#if defined(GTAVC) || defined(GTASA)
            // Vice City / SA: show timer values, comparison, and position
            char debugTimer[256];
            int timeLeft = (int)m_autosaveDisplayUntil - (int)currentTime;
            sprintf_s(debugTimer, sizeof(debugTimer), "Time:%u Until:%u Left:%d Show:%d X:%.0f Y:%.0f",
                     currentTime, m_autosaveDisplayUntil, timeLeft, shouldShow ? 1 : 0, x, y);
            Utils::DrawText(30.0f, 80.0f, debugTimer, 0.4f, 0.8f, CRGBA(255, 200, 100, 255));
#elif defined(GTA3)
            // GTA III: show timer values and force display for testing
            char debugTimer[128];
            sprintf_s(debugTimer, sizeof(debugTimer), "Timer: %u Display: %u Show: %d",
                     currentTime, m_autosaveDisplayUntil, shouldShow ? 1 : 0);
            Utils::DrawText(30.0f, 80.0f, debugTimer, 0.4f, 0.8f, CRGBA(255, 200, 100, 255));

            // Force show in debug mode to test rendering
            shouldShow = true;
#endif
        }

        if (!shouldShow) return;

        // Use the utility function for consistent rendering
#if defined(GTASA)
        Utils::DrawText(x, y, "Autosaved", 1.0f, 2.0f, CRGBA(255, 255, 255, 255));
#else
        Utils::DrawText(x, y, "Autosaved", 1.0f, 2.0f, CRGBA(255, 105, 180, 255));
#endif
    }

    // Switches to controller wording once the player has actually touched a pad.
    // Both input methods keep working regardless of which wording is shown.
    bool ShouldShowControllerHint() {
        return ControllerInput::WasEverUsed() &&
               (m_settings.controllerYesButton != 0 || m_settings.controllerNoButton != 0);
    }

    void DrawRetryPrompt() {
        if (!m_showRetryPrompt) return;

        bool padHint = ShouldShowControllerHint();
        // A button set to "None" in the ini falls back to advertising its keyboard key
        const char* yesLabel = m_settings.controllerYesButton ? m_settings.controllerYesLabel : "Y";
        const char* noLabel = m_settings.controllerNoButton ? m_settings.controllerNoLabel : "N";

#if defined(GTAVC) || defined(GTASA)
        // Use the game's native big message system — same pipeline as "MISSION FAILED"
        // Called every frame with a short TTL so it stays visible until we stop calling it
        if (padHint) {
            // Split with the game's newline token - the button names are too long for one line
            sprintf_s(m_retryPromptText, sizeof(m_retryPromptText), "Retry mission?~n~%s = Yes / %s = No",
                yesLabel, noLabel);
        }
        else {
            strcpy_s(m_retryPromptText, sizeof(m_retryPromptText), "Retry mission? (Y / N)");
        }
        CMessages::AddMessage(m_retryPromptText, 150, 0);
#else
        // GTA III fallback: custom CFont rendering
        if (padHint) {
            sprintf_s(m_retryPromptText, sizeof(m_retryPromptText), "%s - Yes  /  %s - No",
                yesLabel, noLabel);
        }
        else {
            strcpy_s(m_retryPromptText, sizeof(m_retryPromptText), "Y - Yes  /  N - No");
        }
        float centerX = SCREEN_COORD_CENTER_X;
        float topY = SCREEN_COORD_TOP(80.0f);
        Utils::DrawCenteredText(centerX, topY, "Retry mission?", 1.0f, 2.0f, CRGBA(255, 255, 100, 255));
        Utils::DrawCenteredText(centerX, topY + SCREEN_COORD(40.0f), m_retryPromptText, 0.7f, 1.4f, CRGBA(255, 255, 255, 255));
#endif
    }
};

// ============================================================================
// Plugin Entry Point
// ============================================================================
AutosaveMod autosaveModInstance;
