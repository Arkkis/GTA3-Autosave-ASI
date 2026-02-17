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
}

// ============================================================================
// Utility Functions
// ============================================================================
namespace Utils {

    CPlayerPed* GetPlayer() {
        return CWorld::Players[0].m_pPed;
    }

    bool IsOnMission() {
#ifdef GTA3
        if (CTheScripts::OnAMissionFlag == 0) return false;
        int* missionFlag = reinterpret_cast<int*>(&CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag]);
        return (*missionFlag != 0);
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

        int activeCam = TheCamera.m_nActiveCam;
#ifdef GTASA
        TheCamera.m_aCams[activeCam].m_fHorizontalAngle = heading;
        TheCamera.m_aCams[activeCam].m_fTargetBeta = heading;
        TheCamera.m_aCams[activeCam].m_fTrueBeta = heading;
        TheCamera.m_aCams[activeCam].m_fTransitionBeta = heading;
#else
        TheCamera.m_asCams[activeCam].m_fHorizontalAngle = heading;
        TheCamera.m_asCams[activeCam].m_fTargetBeta = heading;
        TheCamera.m_asCams[activeCam].m_fTrueBeta = heading;
        TheCamera.m_asCams[activeCam].m_fTransitionBeta = heading;
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
// AutosaveMod Class - Main mod logic
// ============================================================================
class AutosaveMod {
public:
    AutosaveMod() {
        Events::initGameEvent += []{ Instance().OnGameInit(); };
        Events::gameProcessEvent += []{ Instance().OnGameProcess(); };
        Events::drawHudEvent += []{ Instance().OnDrawHud(); };
    }

private:
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
    } m_settings;

    // ========================================================================
    // State tracking
    // ========================================================================
    
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

    // Debug
    char m_debugText[256] = "";
    char m_saveDebugText[256] = "";
    unsigned int m_saveDebugDisplayUntil = 0;

    // ========================================================================
    // Event Handlers
    // ========================================================================
    
    void OnGameInit() {
        LoadConfig();
        ResetLoadState();
        m_autosaveDisplayUntil = 0;
    }

    void OnGameProcess() {
        unsigned int currentTime = CTimer::m_snTimeInMilliseconds;
        
        DetectGameLoad(currentTime);
        HandlePostLoadState(currentTime);
        HandleAutosave(currentTime);
        HandleMissionRetry(currentTime);
        UpdateDebugInfo(currentTime);

        m_lastGameTime = currentTime;
    }

    void OnDrawHud() {
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
        unsigned char savedHours = CClock::ms_nGameClockHours;
        unsigned char savedMinutes = CClock::ms_nGameClockMinutes;
        unsigned short savedSeconds = CClock::ms_nGameClockSeconds;

        // Attempt to save - check return value
#ifdef GTASA
        CGenericGameStorage::MakeValidSaveName(slot);
        bool saveSuccess = CGenericGameStorage::GenericSave(0);
#else
        bool saveSuccess = PcSaveHelper.SaveSlot(slot);
#endif

        // Restore game time
        CClock::ms_nGameClockHours = savedHours;
        CClock::ms_nGameClockMinutes = savedMinutes;
        CClock::ms_nGameClockSeconds = savedSeconds;

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
            if (CGenericGameStorage::CheckSlotDataValid(Config::MISSION_RETRY_SAVE_SLOT, false)) {
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

        // Handle retry input
        if (m_showRetryPrompt) {
            HandleRetryInput();
        }
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

        if (yPressed && !m_retryYKeyWasPressed) {
            LoadAutosave();
            m_showRetryPrompt = false;
        }
        else if (nPressed && !m_retryNKeyWasPressed) {
            m_showRetryPrompt = false;
        }

        m_retryYKeyWasPressed = yPressed;
        m_retryNKeyWasPressed = nPressed;
    }

    void LoadAutosave() {
#ifdef GTA3
        MakeValidSaveName(Config::MISSION_RETRY_SAVE_SLOT);
        FrontEndMenuManager.m_nCurrentSaveSlot = Config::MISSION_RETRY_SAVE_SLOT;
#elif defined(GTASA)
        CGenericGameStorage::MakeValidSaveName(Config::MISSION_RETRY_SAVE_SLOT);
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

        sprintf_s(m_debugText, sizeof(m_debugText), "near=%d load=%d miss=%d onmiss=%d failtxt=%d",
            nearBlip, m_justLoaded, missionsPassed, isOnMission, missionFailedVisible);
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

    void DrawRetryPrompt() {
        if (!m_showRetryPrompt) return;

#if defined(GTAVC) || defined(GTASA)
        // Use the game's native big message system — same pipeline as "MISSION FAILED"
        // Called every frame with a short TTL so it stays visible until we stop calling it
        CMessages::AddMessage("Retry mission? (Y / N)", 150, 0);
#else
        // GTA III fallback: custom CFont rendering
        float centerX = SCREEN_COORD_CENTER_X;
        float topY = SCREEN_COORD_TOP(80.0f);
        Utils::DrawCenteredText(centerX, topY, "Retry mission?", 1.0f, 2.0f, CRGBA(255, 255, 100, 255));
        Utils::DrawCenteredText(centerX, topY + SCREEN_COORD(40.0f), "Y - Yes  /  N - No", 0.7f, 1.4f, CRGBA(255, 255, 255, 255));
#endif
    }
};

// ============================================================================
// Plugin Entry Point
// ============================================================================
AutosaveMod autosaveModInstance;
