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


## Native 0.2.3–0.2.4: object identity, environment and simulation timing

Protocol 17 adds synchronized source timestamps, clock exchanges on a separate
unreliable channel, complete weather snapshots, object angular motion and native
animation clip durations. This is incompatible with the previously published
protocol 15 build: both peers and a standalone server must use the same build.

Weapon setup now waits for both native weapon models and their animation/TXD
dependencies before GiveWeapon or SetCurrentWeapon. A fresh native crash stack
was inside BlendAnimation during SetAttack with an unprepared weapon animation
group. Cold-start firing tests exercised the pistol, shotgun and rifle in both
processes without reproducing that crash. This fixes resource preparation rather
than hiding a failed animation lookup inside the engine.

Map props bind by model and original map placement, including CDummyObject.
CPopulation's named conversion path replaces that exact dummy; a missing map
instance is awaited rather than cloned. Tracked originals cannot be reclaimed by
population conversion while their network records are alive. The server also
coalesces duplicate registrations of the same non-mission map instance. Falling
objects include angular motion in the moving-update policy. Native diagnostics
count real/dummy instances at each original placement. Sixteen initial props in
the two native processes each had one real instance and no remaining dummy.
Guest damage and a native impulse on a lamppost reproduced the damaged/moving
state on both peers; a sampled position difference after the impulse was 0.0033 m.
The finite 64-prop/192-entity limits remain; this is not a city-wide persistent
campaign destruction ledger, nor does it replay every decorative debris particle.

Weather progression is owned by the host. Guests apply the actual blend, rain,
wind, wet roads, clouds/fog/sun/rainbow/glare, traffic-light brightness, script rain
and lightning state before rendering, while retaining camera-local rain/heat
particles and sound emission. Cached weather follows the reliable join roster;
regular snapshots are replaceable unreliable messages. Native tests switched all
six weather modes and a sunny-to-rain transition in both processes. Thunder
scheduling is implemented but has not had a separate audible native acceptance
check.

Four monotonic timestamps map sample capture time onto the server timeline;
producer staging and network travel are no longer mistaken for fresh arrival.
The estimator excludes server processing, prefers recent low-queue exchanges,
slews corrections, checks echoed pending requests and resets on reconnect.
Entity creates from the same generation cannot overwrite newer moving state.
Background traffic uses sampled interpolation; human-driven vehicle collision
poses retain bounded current-time prediction. Actual animation duration and
measured native phase speed replace the approximation at repeating gait boundaries.

Cooperative gameplay now advances at the original 30 Hz simulation cadence,
independently of the configurable render cap. Fixed timer advances preserve time
scale/pause and fractional milliseconds; bounded catch-up prevents a long stall
from causing an unlimited burst of physics steps. Owned physical transforms and
the camera interpolate only for drawing and are restored before simulation.
Camera interpolation accounts for its reflected Left/Forward/Up basis. Scaled
or invalid bases and teleports bypass rigid interpolation. Replicas refresh at
render cadence without publishing interpolated transforms back into physics.

Two real processes were measured at render caps 30, 60, 144 and 240. Simulation
remained approximately 30 Hz; the last cap achieved about 192 FPS on this PC.
The same three-second native pad-driven run at 30, 60 and 144 FPS ended at exactly
the same recorded coordinates, travelling 16.506 m in each trial. Default drawing
remains 60 FPS. These results do not establish compatibility of every render-side
effect or a complete campaign at arbitrary render rates.

Diagnostics also exposed intermittent multi-second population sample starvation
on localhost. ENet's stock RTT-based unreliable throttle was observed at 0/32
on a server peer, and at 16–24/32 on the source, despite small current RTT and no
outgoing backlog. Peer configuration now measures its RTT minimum over one second,
uses 100 ms acknowledgement probes and faster throttle recovery, retaining ENet's
congestion throttle. Entity send order rotates to avoid deterministic loss
repeatedly selecting the same NPCs. Transport throttle/RTT/queue counters are copied
from the owning network worker into diagnostics; engine access stays on the game
thread. Native stress results are retained outside the source tree, including
failed runs and the untuned 40±15 ms per-direction / 1% loss run; that run still
showed metre-scale corrections and is not evidence of completed WAN support.

Development scene/player placement and test cars now require native ground,
clearance and world checks. Rejected destinations do not unload the current
scene. Test controls expire and test scripts restore input/network configuration;
the private task plan and captures remain outside the repository.


Final local acceptance for 0.2.4: the engine build and all 265 portable/live-UDP/
Unicode-D3D9 checks passed. Two cold population traces after throttle tuning had
maximum observed NPC/traffic source ages of 125 ms and 105 ms, with source
throttle at 32/32 throughout their sampled windows. The two 12-second runs are
bounded acceptance evidence, not a guarantee for all connections. Initial spawn
can still briefly precede binding an NPC occupant. A final two-process native
run repeated all six weather modes, the blended transition, guest prop damage
and a falling original, firing both pistols, ordinary passenger entry and
forward/reverse attachment. Ordinary exits succeeded after the test vehicle was
parked; the earlier attempt while still reversing was correctly refused by the
native game and was not counted as a successful exit.

Damage reconstruction now calls the native load/rebuild path without generating
flying components again. Repeated bonnet/boot/door state changes left the guest's
temporary-object count at 0 rather than duplicating old parts. Native save-load
callers retain the original default behaviour. Windscreen visibility is rebuilt
without repeated particle effects. Detached vehicle debris still needs explicit
network entities/events for matched trajectories/collisions; suppressing duplicate
local spawns is not presented as completed debris synchronization. Exited players
also stop publishing the engine's cached last-vehicle pointer as current occupancy.

## Native 0.2.5: SA-Coop snapshot playback and rendered poses

The previous coordinate/UDP checks did not establish correct animation or effect
attachment. Subsequent user reports of jerking NPCs, rotating torsos and detached
headlight effects remained valid issues after the 0.2.4 acceptance runs.

Adapted the sender-timed snapshot buffer from Karimov-Akbar/sa-coop:
https://github.com/Karimov-Akbar/sa-coop/blob/main/src/client/shared/lib/sync/SnapshotBuffer.h
The read-only local reference's SHA-256 is
`a43e0a3cb2dfa1a3834055ea9bf62dbac60241496ecec9b49ffae59035e6b9e6`.
Copyright (c) 2026 Karimov-Akbar; MIT notice retained in
`coop/vendor/sa-coop.LICENSE`. This is an adaptation of that buffer, not a claim
that all SA-Coop gameplay systems have been transplanted. reVC's existing clock
exchange already maps capture timestamps, so arrival jitter adjusts playback
delay (100–180 ms) without changing sample spacing. Duplicate/older captures are
discarded instead of clearing history, and increasing the jitter margin cannot
play an entity backwards. Human-driven vehicle prediction also rejects old
samples. Owner authority and collision handling retain their existing paths.

The root rendering fault was independent of transport: the simulation-frame
guard prevented NPC roots from being updated on intervening render frames.
Replica roots now refresh every draw, on one shared frame timestamp, with
vehicles positioned before their occupants. The fixed 30 Hz native simulation
continues independently. Occupants of an interpolated owner vehicle receive its
parent transform delta for rendering; the stateful seating routine is not called
again and simulation matrices are restored afterwards.

Snapshots now carry the evaluated local rotations/translations of 22 standard
ped bones, including native IK, look-at and hit reactions. Rotation signs are
handled by shortest-path quaternion interpolation. Signed 16-bit packing keeps
a full NPC update with all six animation layers at 581 bytes; maximum accepted
message size is 768 bytes, below the normal ENet MTU. Local bone translations
have at most approximately 0.25 mm rounding error. Missing bones retain the
animation fallback. Owned offscreen peds evaluate their skeleton and hierarchy
before native IK so a guest's view does not depend on the owner's camera culling.
Animation fallback retains native root-motion extraction and samples the actual
evaluated phase; stripping the extraction flags previously added a repeating
mesh offset on top of its synchronized world root.

Car headlight coronas, including the additional front flares still fading after
passing the camera, follow the rendered model-space attachment points. Previously
the unrefreshed flares could remain at an earlier world position. A bounded
development-only render trace records bone poses, matrix positions and corona
attachment error; it is enabled only by the sandbox trace command.

Verification: the engine and 280 portable/live-UDP/Unicode-D3D9 checks pass,
including full-pose relay, malformed pose rejection, submillimetre packing,
quaternion sign changes, late packets and jitter-buffer monotonicity. Two native
processes were then tested with a 60 FPS host and 144 FPS guest: ambient walking,
nearby gunfire, a guest-originated NPC damage request, ordinary driver/passenger
entry, driving past a front camera, parking and ordinary exits. The final LAN
trace contained 1,765 consecutive rendered NPC comparisons; 97.6% of the selected
locomotion comparisons changed root position on the next draw. The sampled
spine changed by at most 4.68 degrees between consecutive short render frames.
All 178 measured car-headlight attachments had zero computed world-space offset
from their model anchors. These are bounded scene measurements, not a claim of
perfect synchronization of every animation or gameplay scenario.

A further two-process run used an external UDP proxy adding 40±15 ms each way
and 1% packet loss (5,844 forwarded / 55 deliberately dropped datagrams).
Driver/passenger entry, attachment and exit completed without a crash; all 564
measured headlight anchors remained coincident. Pose changes were less regular
than on LAN (largest sampled spine change 21.8 degrees), so this does not close
WAN smoothing or collision convergence. The proxy was stopped and the original
direct port 7781 restored. Campaign progression/cutscenes and detached physical
debris remain outside this verified scope. Private plans and test captures remain
outside the repository; no roadmap or source changes were published in this run.
Final installed binary SHA-256:
`ca984b23a50c50c9b22448f3c52959a8ae20b336e9194c1a40f009513a718e99`
matched the build and both game copies. A final rate sample measured approximately
59.99/143.98 render Hz and 29.99/30.02 simulation Hz in host/guest respectively.
Both were left connected outdoors with normal input, ready pistol resources,
60 FPS limits restored, and no delay proxy running.

## Native 0.2.7: guest AI perception, police and shared pickups

The NPC visual snapshot work in 0.2.5 did not make guest actions part of native
AI perception. Gunshot effects were replayed without the native gunshot event,
and civilian proxy peds failed native IsPlayer checks. Guest-local wanted logic
also spawned a private police helicopter. These were gameplay authority faults,
not grounds to replace ENet.

Remote participants now use passive CPlayerPed instances (with a separate
CPlayerInfo and CWanted) allocated in the native ped pool. They remain replicas:
local player input, camera and physics are not run for them. A main-thread RAII
NPC context selects the actual aggressor/suspect for native AI without swapping
CWorld player slots or camera/input state. Owned NPCs perceive native gunshot,
aim and assault events before their AI step; replicas replay their owner's state.
Damage reactions retain the real source player. Crosshair rays include the
camera offset and use native line-of-sight and ped collision volumes.

Crime reports validate suspect epoch/life and witness ownership/generation.
Native wanted consequences go to the suspect's own game. Pending remote wanted
levels initialize native pursuit limits immediately, so cops do not briefly
choose civilian flight while waiting for a state round trip. Pursuit cleanup
uses the same suspect context when removing a cop or disconnecting a player.
The native HUD retains the local player's wanted level. Remote name tags render
the wanted count using Vice City's own wanted glyph above the nickname.

The session host now runs the native police-helicopter manager for the wanted
participant. Its model request, pool entity, transforms, rotor and searchlight
state are replicated. Guests do not run a second helicopter spawner. Native
special-helicopter destruction remains under the native manager rather than the
generic distance-retirement path, preserving its pHelis lifetime.

Shared supplies use a server registry and stable pickup identities. Existing
native originals are matched before creating replicas; NPC loot records its
source entity and generation. Collection commits availability on the server
before awarding exactly one player through native inventory/effect calls.
Simultaneous claims, repeated revisions, old player lives and distant requests
cannot award the same supply twice. Ammo-only extraction leaves the weapon
available and synchronizes its empty-ammo appearance. Native street cooldowns
run once for the session and wait for participants to leave the radius, including
pickups whose native area is AREA_EVERYWHERE. Consumed temporary loot tombstones
are retired; persistent consumed supplies remain in the late-join baseline.

Read-only references consulted for this adaptation:

- [SA-Coop StoryCoop.cpp](https://github.com/Karimov-Akbar/sa-coop/blob/main/src/client/features/story/cooperative/StoryCoop.cpp): CooperativeStoryShot and owner-side AI events.
- [SA-Coop Bullets.cpp](https://github.com/Karimov-Akbar/sa-coop/blob/main/src/client/features/damage/Bullets.cpp): queued replay before processing.
- [SA-Coop Damage.cpp](https://github.com/Karimov-Akbar/sa-coop/blob/main/src/client/features/damage/Damage.cpp): owner damage reactions and wanted isolation.
- [SA-Coop Pickups.cpp](https://github.com/Karimov-Akbar/sa-coop/blob/main/src/client/features/pickups/Pickups.cpp): shared pickup identity, removal/cooldown and owner loot.
- [SA-Coop Players.cpp](https://github.com/Karimov-Akbar/sa-coop/blob/main/src/client/entities/player/Players.cpp): real player proxies, context and DrawWantedStars placement.
- [SA-Coop Population.cpp](https://github.com/Karimov-Akbar/sa-coop/blob/main/src/client/features/population/Population.cpp): owner simulation and participant context.
- [Parricidium/VCCoop combat.cpp](https://github.com/Parricidium/VCCoop/blob/main/src/combat.cpp): native ReactToAttack is a separate step from InflictDamage.

SA-Coop's MIT notice is retained in coop/vendor/sa-coop.LICENSE. These are native
reVC adaptations, with no copied binary addresses/detours. This section does not
claim that every SA-Coop system has been ported. The wire protocol is now 20;
0.2.7 participants must use matching builds.

Verification: tools/build-coop.ps1 -Test built the native engine and passed
390 portable/live-UDP/Unicode-D3D9 checks. Coverage includes action releases,
aim/wanted wire validation, authority and stale-life rejection, concurrent
collection, partial ammo, consumed late-join baselines, AREA_EVERYWHERE respawn
radius handling and helicopter visual-state validation.

Two actual native processes were tested. Actual guest aim/fire input made three
owned civilians react and flee from the guest; all three replicas followed the
native fleeing state. Explicit damage fixtures separately checked source
attribution and native death/loot, rather than being counted as normal gunfire.
Money awarded +75 only to the guest; health 55 to 100, armour 25 to 100, pistol
ammo +40, and automatic shotgun-slot extraction +20 each applied once. The latter
kept the selected shotgun and left the SPAS pickup visible with zero ammo in both
games. A simultaneous native money collection awarded one total +101 reward.
Corpse money appeared under the same pickup ID in both processes. A real 30-second
street cooldown stayed hidden while the guest stood nearby, then became visible
in both games after both participants left its radius.

An explicit guest hit on a native cop caused pursuit of player 1, guest wanted 2
and host wanted 0. Native cop gunfire subsequently reduced the guest's armour.
The final build repeated the pursuit and cop-removal test and cleared that cop
from the guest pursuit list. At guest wanted 3, both games registered the same
police helicopter (network ID 79 in the final run). The rendered host capture
showed three native wanted badges above Guest while the host HUD remained at 0.
A previous run also observed shared helicopter removal after clearing wanted.

These are bounded sandbox tests. Campaign scripts/cutscenes/progress remain
unported. Shop/property/save/revenue pickups, mines and floating mission packages
are outside the verified shared-supplies path. Arrest handoff, full helicopter
combat/shootdown, all six wanted tiers, reconnect during pickup awards and WAN
police convergence still need dedicated native acceptance tests. None is claimed
complete by the portable suite. Existing collision/WAN smoothing limits remain.

Final installed binary SHA-256:
`7642116d9f901a004f8c182159a510c0aa4d6389ad01831f9616e17113055306`
matched the build and both test game copies. Both games used direct port 7781,
with normal input and 60 FPS limits restored after testing.
Local test scripts, captures and the private roadmap are excluded from commits.
