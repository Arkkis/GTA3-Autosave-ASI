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
#include <GenericGameStorage.h>
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
        if (CTheScripts::OnAMissionFlag == 0) return false;
        int* missionFlag = reinterpret_cast<int*>(&CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag]);
        return (*missionFlag != 0);
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
            state == PEDSTATE_ARRESTED || state == PEDSTATE_ENTER_CAR ||
            state == PEDSTATE_EXIT_CAR || state == PEDSTATE_CARJACK ||
            state == PEDSTATE_DRIVING || state == PEDSTATE_PASSENGER) {
            return false;
        }

        if (player->m_pVehicle && player->m_bInVehicle) return false;

        return true;
    }

    bool IsMissionGiverSprite(unsigned short sprite) {
        switch (sprite) {
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
                return true;
            default:
                return false;
        }
    }

    bool IsPlayerNearMissionBlip(float maxDistance) {
        CPlayerPed* player = GetPlayer();
        if (!player) return false;

        CVector playerPos = player->GetPosition();

        for (int i = 0; i < 32; i++) {
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

        for (int i = 0; i < 32; i++) {
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
        TheCamera.m_asCams[activeCam].m_fHorizontalAngle = heading;
        TheCamera.m_asCams[activeCam].m_fTargetBeta = heading;
        TheCamera.m_asCams[activeCam].m_fTrueBeta = heading;
        TheCamera.m_asCams[activeCam].m_fTransitionBeta = heading;
        player->m_fRotationCur = heading;
        player->m_fRotationDest = heading;
    }

    bool IsMissionFailedTextVisible() {
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
    }

    void DrawText(float x, float y, const char* text, float scaleX, float scaleY, CRGBA color) {
        CFont::SetJustifyOff();
        CFont::SetBackgroundOff();
        CFont::SetScale(scaleX, scaleY);
        CFont::SetFontStyle(FONT_HEADING);
        CFont::SetPropOn();
        CFont::SetColor(color);
        CFont::SetDropShadowPosition(2);
        CFont::SetDropColor(CRGBA(0, 0, 0, 255));

        wchar_t wtext[256];
        AsciiToUnicode(text, wtext);
        CFont::PrintString(x, y, wtext);
    }

    void DrawCenteredText(float x, float y, const char* text, float scaleX, float scaleY, CRGBA color) {
        CFont::SetCentreOn();
        CFont::SetCentreSize(500.0f);
        DrawText(x, y, text, scaleX, scaleY, color);
        CFont::SetCentreOff();
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

        bool needSave = false;
        if (config["Debug"].isEmpty()) {
            config["Debug"] = 0;
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
        bool saveSuccess = PcSaveHelper.SaveSlot(slot);

        // Restore game time
        CClock::ms_nGameClockHours = savedHours;
        CClock::ms_nGameClockMinutes = savedMinutes;
        CClock::ms_nGameClockSeconds = savedSeconds;

        // Only update timers and show notification if save succeeded
        if (saveSuccess) {
            // Update the appropriate cooldown timer based on slot
            if (slot == Config::MISSION_COMPLETE_SAVE_SLOT) {
                m_lastMissionCompleteAutosaveTime = currentTime;
            } else if (slot == Config::MISSION_RETRY_SAVE_SLOT) {
                m_lastNearBlipAutosaveTime = currentTime;
            }

            // Only show notification if one isn't already being displayed
            // This prevents multiple "Autosaved" texts from appearing
            if (currentTime >= m_autosaveDisplayUntil) {
                m_autosaveDisplayUntil = currentTime + Config::AUTOSAVE_DISPLAY_DURATION_MS;
            }
        }

        return saveSuccess;
    }

    // ========================================================================
    // Mission Retry Feature
    // ========================================================================
    
    void HandleMissionRetry(unsigned int currentTime) {
        int missionsPassed = CStats::MissionsPassed;
        bool isOnMission = Utils::IsOnMission();

        // Reset on load/new game
        if (m_justLoaded || m_lastMissionsPassed == -1 || missionsPassed < m_lastMissionsPassed) {
            ResetMissionRetryState(missionsPassed);
        }

        // Clear prompt if a new mission starts while it's visible
        if (m_showRetryPrompt && isOnMission && !m_wasOnMission) {
            m_showRetryPrompt = false;
        }

        m_wasOnMission = isOnMission;

        // Primary detection: Check if "Mission Failed" text is visible on screen
        bool missionFailedTextVisible = Utils::IsMissionFailedTextVisible();
        
        // Show prompt whenever mission failed text appears (simple and reliable)
        if (missionFailedTextVisible && !m_wasMissionFailedTextVisible) {
            // Mission failed text just appeared - show retry prompt if we have a save
            if (CheckSlotDataValid(Config::MISSION_RETRY_SAVE_SLOT)) {
                m_showRetryPrompt = true;
            }
        }
        
        // Track mission failed text visibility (prompt stays visible until user interacts)
        m_wasMissionFailedTextVisible = missionFailedTextVisible;

        // Autosave on mission success
        if (missionsPassed > m_lastMissionsPassed) {
            m_lastMissionsPassed = missionsPassed;
            m_showRetryPrompt = false;
            m_pendingMissionCompleteSave = true;
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
        MakeValidSaveName(Config::MISSION_RETRY_SAVE_SLOT);
        FrontEndMenuManager.m_nCurrentSaveSlot = Config::MISSION_RETRY_SAVE_SLOT;
        FrontEndMenuManager.m_bWantToLoad = true;
        FrontEndMenuManager.m_bWantToRestart = true;
        b_FoundRecentSavedGameWantToLoad = true;
    }

    // ========================================================================
    // Debug & HUD Drawing
    // ========================================================================
    
    void UpdateDebugInfo(unsigned int currentTime) {
        if (!m_settings.debugMode) return;

        bool nearBlip = Utils::IsPlayerNearMissionBlip(Config::MISSION_BLIP_DETECTION_RANGE);
        int missionsPassed = CStats::MissionsPassed;
        bool isOnMission = Utils::IsOnMission();
        bool missionFailedVisible = Utils::IsMissionFailedTextVisible();

        sprintf_s(m_debugText, sizeof(m_debugText), "near=%d load=%d miss=%d onmiss=%d failtxt=%d",
            nearBlip, m_justLoaded, missionsPassed, isOnMission, missionFailedVisible);
    }

    void DrawDebugInfo() {
        if (!m_settings.debugMode || m_debugText[0] == '\0') return;

        CFont::SetDropShadowPosition(1);
        Utils::DrawText(30.0f, 30.0f, m_debugText, 0.4f, 0.8f, CRGBA(255, 200, 100, 255));
    }

    void DrawAutosaveNotification() {
        if (static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds) >= m_autosaveDisplayUntil) return;

        // Position relative to bottom-left corner (30px from left, 60px from bottom)
        float x = SCREEN_COORD_LEFT(30.0f);
        float y = SCREEN_COORD_BOTTOM(60.0f);
        Utils::DrawText(x, y, "Autosaved", 0.8f, 1.5f, CRGBA(255, 255, 255, 255));
    }

    void DrawRetryPrompt() {
        if (!m_showRetryPrompt) return;

        // Position at top of screen, centered horizontally
        float centerX = SCREEN_COORD_CENTER_X;
        float topY = SCREEN_COORD_TOP(80.0f);  // 80px from top
        
        // Larger text for better visibility
        Utils::DrawCenteredText(centerX, topY, "Retry mission?", 1.0f, 2.0f, CRGBA(255, 255, 100, 255));
        Utils::DrawCenteredText(centerX, topY + SCREEN_COORD(40.0f), "Y - Yes  /  N - No", 0.7f, 1.4f, CRGBA(255, 255, 255, 255));
    }
};

// ============================================================================
// Plugin Entry Point
// ============================================================================
AutosaveMod autosaveModInstance;
