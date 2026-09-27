#include <Globals.h>

// ── API client ────────────────────────────────────────────────────────────────
ApiClient* api = nullptr;

// ── Timing ────────────────────────────────────────────────────────────────────
unsigned long lastScreenTime  = 0;
unsigned long lastTapTime     = 0;
unsigned long lastWakeupTime  = 0;
unsigned long lastHeartbeat   = 0;
unsigned long lastTick        = 0;
unsigned long lastFade        = 0;
unsigned long messageTime     = 0;
unsigned long lastPulse       = 0;

unsigned long countdownStartMs    = 0;
int           countdownTotalSeconds = 0;
unsigned long lastWifiCheck;
unsigned long lastWifiConnected;
unsigned long lastSuccessfulHeartbeat ;

// ── Timer state ───────────────────────────────────────────────────────────────
unsigned long countdownSeconds      = 0;
unsigned long maxSeconds            = 0;
long          timesUpMessageDelay   = -1;
bool          runOnce               = false;
bool          isOutOfOrder          = false;
int           optimisticSeconds     = 0;

// ── Display state ─────────────────────────────────────────────────────────────
int fadeValue = 0;
int fadeDir   = 5;

// ── Machine config defaults ───────────────────────────────────────────────────
// These are overwritten by fetchConfig() on boot.
// Values here are the safe fallback if the server is unreachable.
bool          timerMode           = true;
bool          useMachineBusy      = false;
bool          coinMode            = false;
int           coinCount           = 7;
int           coinPulseDuration   = 70;
int           coinPulseDelay      = 200;
int           busyCooldownSeconds = 1200;
unsigned long cycleLengthSeconds  = 100;
double        Amount              = 0.25;
int           screenTimeout       = 30000;