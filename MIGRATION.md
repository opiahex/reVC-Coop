# VC:COOP: source-engine migration

Started 2026-10-23. Upstream: https://github.com/mrxenginner/reVC, branch miami,
revision a26fff4af555a8a425ac9f17d1dd718d00d1f942.
librw submodule: 0ad67ee226ea51beb3f465cb8857195e4db16029.
Upstream README is archived in docs/UPSTREAM_README.md; LICENSE.md is retained. This is a separate native-engine
experiment; ../vc-coop remains the preserved ASI prototype.

Build target: Windows x86, MSVC 2022, librw Direct3D 9, Miles audio.
The CMake D3D9 selection is checked by the configured platform string because
the backend boolean lives inside librw's subdirectory scope; otherwise the
Windows skeleton attempts Direct3DCreate8 despite D3D9 headers.

Reusable: ENet server/client/worker, validated protocol, interpolation, chat
settings and UI behavior. Game binding and rendering backend are adapted.
Unported features must remain explicitly listed; no perfect sync is promised.
The original game data and user saves are not part of this repository.

## Native prototype reVC 0.1.1

The cooperative module is compiled into reVC.exe. The original gta-vc.exe and
ASI loader are not used. Explicit game lifecycle, frontend, render and Windows
message calls replace the original injection layer. The existing ASI folder
is preserved and SA:COOP is a read-only reference.

Ported: validated ENet protocol/server/background worker, connection menu,
Cyrillic chat, fixed default chat appearance with /fontsize and /pagesize,
player snapshots, interpolation, native CPed-derived remote actors and animation
associations, name/health/armour/weapon-icon/bubble overlay, teammate radar blips.
Native actors are non-colliding visual replicas in this first stage. Combat,
vehicle authority/seats/damage, NPC/traffic replication, story scripts, marker
barriers and cutscene/audio replay are NOT ported yet. Sandbox suppresses story
execution after the initial local player has been created. This is not a
campaign-compatible replacement for the ASI build.

Native protocol is 11, deliberately rejecting ASI protocol 10 sessions.
For the source branch the weapon-state range includes the native melee contact
state. Native actors use named engine fields/enums and normal C++ overrides;
no absolute code addresses, detours or changed vtables are used.

## Build and run

Requirements: Windows, CMake, MSVC 2022 x86 tools. From PowerShell run
`tools/build-coop.ps1 -Test`. Build output stays in this task's work/revc-build.
The script fails on missing tests as well as build/test failures.

For local testing the separate Host and Guest folders in work/revc-live-test
contain only game resources, upstream gamefiles, reVC.exe, Miles and mpg123.
They do not contain the former ASI/loader/graphics-patch DLLs. Original game
folders and saves are untouched. Each source engine uses its own local
userfiles folder. Their native config uses UDP 7781; Host starts an embedded
server. Guest waits for a ready host before starting the native sandbox.

## Verification and known limits

Built the x86 engine, native module, server and regression executable.
CTest passes wire validation/truncation/version rejection/interpolation,
live two-client UDP state/chat/background hold/reconnect checks, actual D3D9
Cyrillic glyph pixels and render-state restoration, and Unicode WM_CHAR input
through the chat overlay. These are meaningful subsystem checks, not a claim
that the whole original ASI test suite or campaign has been ported.

Two native processes reached the sandbox, created each other's engine actors,
and exchanged positions according to their own game-thread diagnostics.
The user reported Cyrillic keyboard mojibake, invisible remote bodies and
background cursor recentering in 0.1.0. In 0.1.1 the Win32 class/window and
default procedure use Unicode; replicas explicitly set their native clump
alpha to 255 (normal CPed control normally performs this fade-in); mouse
polling/centering now checks the actual OS foreground window. Teammate blip
scale increases from 1 to 2. Visual confirmation of these fixes is pending.

Retained reusable code attribution: coop/vendor/vc-coop-LICENSE,
coop/vendor/vc-coop-THIRD_PARTY_LICENSES.txt, coop/vendor/sa-coop-ui.LICENSE,
and ENet's license inside coop/vendor/enet. Upstream engine and librw notices
remain in place. Original game assets are not distributable source files.

### 0.1.2 render transform correction

User screenshots confirmed Cyrillic input works in 0.1.1 but replica bodies
remained invisible despite their alpha being 255. The adapter called
CEntity::UpdateRwFrame, which updates frame objects but does not copy the
entity CMatrix into its attached RenderWare frame. Normal CPhysical processing
performs that commit; visual replicas intentionally bypass that simulation.
The adapter now explicitly calls GetMatrix().UpdateRW() before UpdateRwFrame().
Native diagnostics export both entity position and render-frame position so
this invariant can be verified in real engine instances.

### 0.1.3 safe join placement and final local evidence

Join placement no longer adds a fixed +2 to the host's X coordinate. A tested
search enumerates nearby positions, verifies loaded ground and slope, checks
body/head clearance against buildings/vehicles/objects, checks line of sight
from the host, rejects a different storey and avoids occupied player positions.
When every candidate is blocked it keeps the join pending with a chat message
and retries; it never chooses an unchecked fallback or publishes a ready pose.

149 subsystem checks pass, including the new blocked-spawn/no-ground/wrong-floor/
occupied-player cases. Two 0.1.3 native processes loaded and exchanged actors.
A live rejoin with the host at the reported wall/doorway position selected an
unobstructed point 1.6 metres away at the same ground level. Both diagnostics
show alpha 255 and identical entity/render-frame coordinates. The user visually
confirmed Russian chat input in 0.1.1 and remote Tommy visibility in 0.1.2.
The foreground cursor guard is implemented; no explicit user confirmation of
that part has been received. Current two-process evidence is saved locally in
work/revc-native-validation outside the source repository. Tests do not yet
validate vehicles, combat, shared population or the campaign in this branch.

To restart the existing local pair, close those two test windows and run
`tools/run-local-pair.ps1` (optionally `-Build`). This script validates host
readiness before starting Guest and only targets the separate native runtime.


## Native world and combat 0.2.1 (2026-10-23)

Protocol 14 now carries native ped type and optional procedural aim bones,
vehicle wheel angular motion/suspension/doors/panels/lights and bike lean,
projectile objects, owner-routed hits/contacts and explosion events. This
supersedes the unported-world list in the historical 0.1.1 section above.
All bindings use named source classes and pool handles on the game thread.
No game addresses, binary detours or manual field offsets were introduced.

NativeWorld shares traffic vehicles, their real NPC drivers/passengers,
walking NPCs and nearby interactive world objects. One client simulates each
entity; nearby population uses one generation authority. Claiming a vehicle
transfers its NPC occupants and advances generations. G reserves a free seat
before the local native get-in animation and cannot evict another player.
Replicas replay animation layers, weapon models and native seat transforms;
the owner retains stock AI, physics, entry/exit and damage processing.
Player vehicles transmit at approximately 30 Hz, moving population 20 Hz;
stationary objects use 2 Hz. The receiver interpolates copied snapshots.

Combat records the native bullet ray/impact without firing a second damage ray
on the receiver. Muzzle flashes, tracers, impact sparks/smoke/blood and gunshot
audio use engine effects. Shotgun pellets and sniper impacts distinguish
muzzle/audio/tracer stages. Victim owners apply hits once; life/session/entity
generations reject stale authority. Grenades/rockets are shared physical object
snapshots; explosion effects are broadcast once and each process damages only
its own simulated entities. Vehicle contacts transfer bounded impulses and
suppress an impulse already solved locally by the target owner.

### Reproduced defects and corrections

- NPC/occupant skins: animation associations alone were insufficient. Offscreen
  owners may retain bind-pose frame data, and occupants can bypass the normal
  ped PreRender pass. Only procedural aiming overrides bone quaternions; full
  replica animation/skin matrices are committed immediately before drawing,
  including the vehicle's direct occupant render path. Native render-target
  readbacks show walking NPCs and car drivers in Guest, not just non-null pointers.
- Bikes: CBike::CalculateLeanMatrix cached the first replica transform because
  normal ProcessControl was bypassed. Riders were measured over 150 metres
  from their moving bike. Applying a bike snapshot now invalidates/rebuilds
  that cache and replicates/interpolates the source lean angle.
- Windscreen freeze: CDamageManager::ApplyDamage writes a windscreen pseudo-light
  into bit 8 of m_lightStatus (upstream even comments on this). The old wire
  light validation rejected the complete vehicle snapshot after that damage.
  Capture now serialises the four actual eLights slots through native getters;
  the windscreen remains in panel damage. A damaged moving car no longer stops
  receiving coordinates/health/parts on the peer because of an unused light bit.
- Seating applies named occupant slots and native references, not an AddPassenger
  force every snapshot. An in-vehicle ped does not collide with its own car.
  Pool generations prevent reuse of stale native entities, and a destroyed
  short-lived projectile cannot be recreated by its delayed creation ACK.
- The 30 FPS native limiter is enforced throughout the online session, including
  after menu changes, and the previous local setting is restored on disconnect.
  This preserves the original physics cadence; uncapped rendering separated
  from simulation has NOT been implemented or claimed.

### Additional world-state corrections

Native one-bit engine fields must receive booleans, not the numeric flag mask:
assigning 2/4/8/16 to a one-bit unsigned field truncates to zero. Replica object
collision, cracked/broken glass, damaged rendering and vehicle lights/scorch
now use explicit boolean conversions. The native object test changed a street
prop from flags 3 to 19 and both instances retained collision and damaged state.
Destroyed map props retain a hidden/non-colliding tombstone rather than restoring
an intact peer original when the physical native object is deleted. Tombstones
still share the current bounded object pool policy; full-city paging is future work.
Object state changes are sent promptly even when the object is stationary.

Client snapshots now include the client's own successfully published player and
vehicle state. The server deliberately echoes updates only to other peers; using
only that table previously omitted the local viewer from population/occupancy
checks. Retirement also checks the actual current native position and explicitly
protects the local vehicle. Live UDP regression tests cover self-state freshness.

### Verification and limits

The x86 engine/server/subsystem suite builds and CTest passes 185 checks,
including locally published self/vehicle snapshots without server echo,
full six-layer NPC animation/aim-bone wire packets, bike-lean
interpolation, live UDP ownership transfer with NPC occupants, contested seat
reservations, owner-routed NPC damage, guest projectile creation/explosion,
Cyrillic input and actual D3D9 glyph/render-state checks.

Two actual native game processes additionally exercised normal passenger entry,
reverse with a passenger (about 22 metres), a damaged-windscreen passenger drive
(about 44 metres), matching missing bonnet/panel state, separate human vehicle
owners and a collision that displaced the target about 10 metres and damaged
both cars without a crash. In the windscreen regression the source light storage
was 256, the wire carried only real light slots, matching panel state remained,
and Guest kept receiving fresh car snapshots (observed age at most 65 ms during
the sampled run). A single 30-point test hit applied once to Guest armour
(100 to 90.1 with the stock player multiplier); one native pistol impact and one
explosion each produced one remote visual event. A Guest grenade appeared as a
shared object and was removed after its explosion. These opt-in engine commands
run through native APIs; they are not process-memory or desktop-input automation.
Native JSON diagnostics, BMP render readbacks and crash dumps/PDBs stay under
work/revc-native-world-validation / work/revc-live-test outside the source tree.

This is still a development cooperative engine branch. Campaign VM/mission
stages, mission markers/barriers, cutscene/audio state and saved-game co-op have
not been ported from the ASI prototype; the test sandbox still suppresses story
scripts. Two-process tests do not establish whole-campaign compatibility or
zero desynchronisation. Three real game instances, high-latency/WAN ownership
handoffs, special continuous weapon effects (fire/teargas), water/aircraft combat
and persistent destruction after streaming/rejoin need further integration work.
A final native NPC damage check changed health 100 to 67 on the owner and
observer, and a PCJ-600 carried both human players for about 33 metres with the
Guest passenger approximately one metre from its bike throughout the sampled
ride. A saved Guest render-target frame shows both human riders in their seats.
The protocol has server authority/epoch/range checks but is not a production
anti-cheat implementation. Original ASI and SA:COOP sources remain unchanged.


## reVC 0.2.2: vehicle timing and collision reconciliation

The report that the 0.2.1 vehicles diverged remains a real failed user test;
the earlier stationary agreement did not establish motion/collision correctness.
The live processes had closed before the report could be captured. Their last
saved stationary coordinates agreed, so this iteration separately records
physical, render-frame and received positions and tests moving trajectories.
A native 0.2.1 drive reproduced several metres of moving disagreement. Vehicle
collision poses were using the same intentional 100 ms playback delay as NPCs.

Vehicles now use a dedicated bounded motion predictor, including angular and
wheel motion. Small receipt corrections decay over 40 ms; teleports/authority
changes reset correction, and extrapolation stops after 100 ms without a sample.
Pedestrian playback retains its existing buffer. Vehicle world and render poses
use the same transform. After the stock world contact solver, remote vehicle
copies are reconciled before camera processing; a local contact displacement
must not accumulate into the other owner's final replica pose. Local passengers
and NPC occupants are seated again against that reconciled transform.

Protocol 15 carries a validated 0..250 ms producer staging age on entity updates.
The worker measures it from the copied game-thread snapshot, the server relays
it and receivers date the sample accordingly. This compensates the wait before
sending without comparing clocks on different machines. It does not compensate
all WAN propagation delay. Protocol 14 clients cannot join this build.

The final x86 build and CTest pass 199 checks, including straight/turning vehicle
prediction, bounded stalls, settling after braking, correction reset on ownership
change, wheel phase, age validation and live UDP preservation of source age.
The network handoff test now waits for both clients to receive the grant before
publishing as the new owner, rather than racing one client's acknowledgement.

Two real game processes exercised independent human drivers, broken windscreens,
acceleration, turns, reverse and contacts. At approximately 22 m/s, the sampled
same-clock trajectory comparison had median errors about 0.26/0.30 m and 95th
percentile errors about 0.57 m in the two directions; maximum observed error was
1.45 m. These are approximate comparisons interpolated between diagnostic frames
on one PC, not a guarantee for arbitrary latency or collisions. Native physical
and render-frame positions agreed in these samples. After the controlled contact,
both cars had matching health/panel damage and positional differences about
0.023 m and 0.0001 m, respectively; neither process crashed. A further passenger
ride moved about 16 m, with the passenger staying within 0.85 m of the vehicle
origin and receiving car samples up to 95 ms old. Engine framebuffer readbacks
showed both human occupants seated. Camera close-ups obscured by building walls
are not evidence of vehicle geometry correctness.

Opt-in sandbox telemetry retains two bounded CSV pages per process under each
local runtime (vc-coop-vehicles-PID-0/1.csv), approximately the last 20 minutes at
10 Hz. It contains vehicle authority, sequence, sample age, physical/render pose,
velocity and health, and survives closing a test window. Test-only vehiclepose
and short trace commands enable repeatable native setups without memory writes.
The remaining moving error, WAN behaviour, detached debris, mission/campaign and
previously listed limitations are not declared solved by this iteration.


## Repository presentation and opt-in capture helpers

The Russian/English reVC-Coop README uses an original SVG wordmark and two
unaltered native Host frames. The upstream README is archived unchanged in
docs/UPSTREAM_README.md; upstream notices and third-party licences remain.
The user elected to supply their own GIFs, so both README media slots currently
resolve to real PNGs rather than missing files or invented demonstrations.

Sandbox-only development commands support short native framebuffer sequences,
fixed camera scouting, ordinary pad-driven movement/jump/fire, scene setup and
presentation HUD visibility. They do not alter transport or normal game input
outside the opt-in sandbox. Recording is bounded to 12 seconds and 30 FPS,
uses validated local tags and writes raw frames/manifests outside source.
The final C++ build and all 199 subsystem checks passed. Failed scouting/recording
attempts are not included as public demonstrations. After the user stopped the
recording work, no additional gameplay/GIF outcome is claimed.

## Publication: supplied clips and configurable online frame limit

Both READMEs now use a plain reVC-Coop title and factual version/check/game/licence
badges. The rejected SVG banner, placeholder comments and media instruction page
were removed. The user supplied the two motorcycle clips: host driving and guest
passenger. Their external black canvas was cropped without resizing the game
image, to 1270x700 and 1020x578 respectively. The looping GIFs retain the full
3.73/4.10-second clips at 30 frames per second, with per-frame 256-colour palettes.
GIF colour quantisation is inherently lossy; colour-accurate cropped video masters
remain outside the repository. Midpoint frames were inspected for crop and content.

Online sessions default to 60 FPS. The [native] fps_limit setting accepts a
positive integer or 0 to disable the limiter, and is read on engine startup.
Invalid values fall back to 60. The session saves and restores the pre-connection
limiter settings; even uncapped mode leaves a nonzero menu timing divisor.
This changes the cap, not Vice City's frame-dependent physics. Both READMEs warn
that high limits may break physics, NPC movement and animation.

The final engine build and CTest pass 209 checks, including custom/uncapped/invalid
limits and restoration across sessions. Startup diagnostics from the two deployed
native test processes reported ready worlds, fpsLimit=60 and limiterEnabled=1.
Diagnostics serialize the int8 limiter flag numerically so the JSON stays valid.
These checks do not establish correct physics at arbitrary FPS or campaign support.

Legacy upstream workflow definitions remain available by manual dispatch only:
their premake/platform matrices do not build the native cooperative target. The
tested CMake build is documented in docs/BUILDING.md; the README's check badge
records local subsystem results and does not claim a completed GitHub CI run.
