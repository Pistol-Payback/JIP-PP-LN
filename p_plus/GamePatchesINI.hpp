#include <windows.h>
#include <filesystem>
#include <fstream>

static constexpr char kIniPath[] = "Data\\NVSE\\plugins\\jip_nvse.ini";

static constexpr char kDefaultIni[] = R"( [GamePatches]
bIgnoreDTDRFix=1
;	Fixes the Ignore DT/DR effect (mostly found in some melee/unarmed weapons), which is completely broken in the game.
;	In addition, modifies the game's damage-reduction calculation, such that DT is subtracted BEFORE DR is applied.

bEnableFO3Repair=0
;	Brings back the FO3-style item repair menu to FNV. The max repair amount of items will be capped to the player's
;	(Repair Skill * 0.6) + 40. For example, with 50 skill, items can only be repaired for up to 70% of their max health,
;	as opposed to 100% as before, regardless of skill level.

bEnableBigGunsSkill=0
;	Restores Big Guns as a fully-functional, playable skill.

bProjImpactDmgFix=1
;	Fixes an engine bug where weapons which fire projectiles that explode upon impact (i.e. Missile Launcher, Grenade
;	Launcher, etc.) would only apply the damage from the explosion, even on direct hit, ignoring the weapon's hit damage.

bGameDaysPassedFix=1
;	Fixes an engine bug where the 'GameDaysPassed' and 'GameHour' global timers would "freeze" and stop increasing in
;	game real-time (they would only be increased when sleeping, waiting or fast traveling). This issue directly affected
;	hardcore needs build-up, which would also freeze in game real-time.

bHardcoreNeedsFix=1
;	Fixes an issue where some hardcore needs could, for undetermined reasons, end up having negative values. This resulted
;	in hardcore needs not increasing at the correct rate/at all.

bNoFailedScriptLocks=1
;	Fixes an engine behavior where a script that has failed (due to any reason) at some point during execution will be
;	effectively disabled by the game and will no longer be processed again until the game is restarted.

bDoublePrecision=1
;	Modifies the game's code such that arithmetic/relational operations in scripts are calculated/evaluated with
;	double-precision floating-point accuracy (instead of single-precision). This was causing various issues, where
;	relational operators (==, !=, <=, <, >=, >) were not evaluating correctly, and numeric calculations ended with
;	inaccurate results (this was especially observed with relatively high absolute values). Additionally, this patch also
;	guarantees no script errors/crashes in cases of division by zero.

bQttSelectShortKeys=1
;	If enabled, (a) when selecting an item stack in either the inventory, container, or barter menus, holding SHIFT will
;	transfer/drop the entire stack, whilst holding CTRL will transfer/drop a single item from the stack - thus skipping
;	the quantity-select menu. (b) Picking up from a container a stack of items that are weightless (such as ammo on
;	non-hardcore mode) will bring up the quantity-select menu, instead of automatically picking the entire stack.

bLocalizedDTDR=0
;	When enabled, a distinction will be made between head-armor and body-armor when applying damage reduction from DR/DT.
;	Head hits will benefit only from DT/DR gained from worn head-armor (if any), whereas body hits, in similar fashion,
;	only from DT/DR gained from worn body-armor. (Note: Requires bIgnoreDTDRFix to be enabled).

bVoiceModulationFix=1
;	Adds voice modulation (a slight distortion effect) for talking activators and holotapes.

bSneakBoundingBoxFix=1
;	Fixes a longstanding Bethesda games' bug where the dimensions of the collision bounding box encapsulating the player
;	(as well as all NPCs) remained fixed and did not scale to correspond to body posture. This, effectively, had made it
;	impossible to crawl through breaches and spaces when crouched, despite being able to easily fit through them.
;	This patch fixes this issue for the player character and human companions.

bEnableNVACAlerts=0
;	If NVAC is installed, enable this setting to receive in-game alerts in the event an exception has occurred in the game's
;	code that was successfully handled by NVAC. You will be notified by a corner message, and the error's details will be
;	printed to the console. This should make it a lot easier to identify the precise circumstances/location in which the
;	error has occurred.

bLoadScreenFix=0
;	Forces the load menu to give priority to location-specific load screens (if any are found to match current location)
;	when selecting a load screen to display.

bNPCWeaponMods=0
uWMChancePerLevel=2
uWMChanceMin=10
uWMChanceMax=60
;	When enabled, all NPCs will have a chance for their main weapon to include weapon mods. The chance is based on each NPC's level -
;	the higher the level, the greater the chance. The type of mod(s) is selected randomly from the ones available for the particular
;	weapon. Weapons have a chance to include multiple mods, though 2 mods is rare, and a fully-modded weapon is uncommon.
;	uWMChancePerLevel controls chance % increase per NPC level.
;	uWMChanceMin and uWMChanceMax set the minimum/maximum possible chance.
;	Formula: Chance(%) = MinOf( uWMChanceMax , MaxOf( uWMChanceMin , Level * uWMChancePerLevel ) )

uNPCPerks=0
;	Unlocks perks for NPCs (perks will no longer be restricted to the player character and player teammates).
;	This option has 2 modes:
;	uNPCPerks=1 : Script commands such as AddPerk/RemovePerk/HasPerk/etc. will work on ANY NPC in the game.
;	uNPCPerks=2 : (In addition to the above) Perks will automatically be added to human NPCs:
;				  * 1 random starting Trait.
;				  * 1 random Perk for every 3 levels of the NPC, up to 10 perks max.

bCreatureSpreadFix=0
;	Fixes a bug where all non-human actors suffer a massive penalty to weapon-spread (equal to the fUnaimedSpreadPenalty game setting)
;	due to not having weapon aiming animations, and therefore not technically being able to aim. Note that although this is almost
;	certainly a bug and an oversight of the game devs, this fix will make certain enemies much deadlier and may affect game balance.
)";