# GRAVE PRIX – design and data contract

This file is the contract between the engine and the content. Every data file, every agent and every piece of code
follows it. When the engine changes a format, it changes this file first.

## The game

GRAVE PRIX is a top-down combat racer for PC and Steam Deck (C++20, raylib 5.5, real-time 3D seen from above,
fixed north-up camera that zooms out with speed and looks ahead the way the car goes). Six cars race on closed circuits with walls; every car has a front
gun and can carry mines, rockets, spikes, oil, smoke and nitro. Wrecks stay on the track, burning.

The **career** is the heart of it: an underground racing league run by **Mortimer Graves**, owner of the funeral
group *Graves & Sons*, who earns twice from every race – from the betting and from the funerals. The player starts
in the lowest of five leagues with a rusty car and little money, races on race days (three offers per day), earns
prize money and bounties, buys and upgrades cars in the garage, buys weapons on the black market, borrows from a
loan shark, picks a sponsor, reads the tabloid after every race day, and climbs league by league. Each league ends
with a showdown against its champion; at the top waits Graves himself in his armoured hearse.

Other modes: **Quick race** (any unlocked track and car), **Derby** (an arena: the last car driving wins),
**Time trial** (alone, against the ghost of the best lap).

### Tone

Black humour as in the 90s combat racers: puns on death, funerals, insurance and car crashes; cheerful cynicism;
the funeral industry as the sponsor of the carnage. Never gore for its own sake, no real persons, no real brands,
no slurs. Every rival has a pun name and a personality.

### Originality (important)

GRAVE PRIX is an **original game in the genre** of top-down combat racing. It must not reuse anything specific
from Death Rally (1996/2012) or other games: no names of its drivers, cars, tracks, characters, items or places, no
quotes, no copied layouts, no copied music. Genre mechanics (racing for money, upgrades, weapons, a ranking, a final
boss) are fine; the content is ours.

### Languages

German and English. German is written in the code and data; English sits beside it (`title_en`, `desc_en`, ...)
and in `data/lang/en.txt` for the interface (see Lang.h). Every text a player sees exists in both languages. Car
names, rival names and the game's name may be English pun names in both languages, but texts are translated.

## Units and directions

- Metres and seconds. The ground is the plane y = 0, y is up.
- On the ground: x grows to the right (east), z grows **down the screen** (south).
- Headings and `rot` values in data are **degrees, 0 = up the screen (north, -z), clockwise**: 90 east, 180 south,
  270 (or -90) west. The forward vector of heading a is (sin a, -cos a) in (x, z).
- A car is ~4.0–5.8 m long and 1.7–2.3 m wide. Roads are 11–18 m wide. A lap is 600–1800 m. Top speeds are
  30–62 m/s (108–223 km/h).
- The camera sees about 70 m from top to bottom at rest and up to ~110 m at full speed.

## Data syntax

Everything lives in `data/` as plain UTF-8 text; the game reads every `.txt` below `data/`.

```
# a comment (also after a line's words: " # ...")
car rustbucket                   <- a block: its type and its name, at the start of the line
  title "Rustbucket"             <- the block's lines are indented (two spaces)
  price 0 mass=1050              <- key=value words are named values, other words are in order
  desc "Two words" desc_en "..." <- quotes keep words together
> A line of prose                <- lines starting with ">" are prose of the block (stories, news bodies)
```

- Names are lower-case ASCII with `_`. A name is unique per block type across all files.
- Numbers may be ranges `a..b` where noted: the value grows from a (level 0) to b (the top level).
- Colours: `#rgb`, `#rrggbb`, `#rrggbbaa` or names: white black steel gunmetal graphite silver ivory sand rust
  crimson red orange amber gold yellow lime green olive teal cyan sky blue navy violet magenta pink bone flesh moss
  chitin plasma ember. In car models also `paint`, `paint2`, `paint3` (the driver's colours, see Models).
- `GravePrix --validate` lists errors and warnings for all data; it must report 0 errors.

## Models (`data/models/*.txt`)

Cars, props, pickups and wrecks are built in code from parts (the Forge). Flat-shaded facets with a little
brightness noise; a colour's `emit` says how much it glows.

Model space: x right, y up, **-z is the nose / front**. One unit is one metre. The origin is the middle of the
footprint on the ground (y = 0 is the ground the wheels stand on).

```
model car_rustbucket
  part body len=4.1 wid=1.72 hgt=0.62 y=0.32 color=paint round=5 nose=0.22 tail=0.12 drop=0.35
  part cabin len=2.0 wid=1.56 hgt=0.5 y=0.94 z=0.25 front=0.4 back=0.25 top=0.86 color=paint glass=#1a2228
  part wheel r=0.31 w=0.2 x=0.74 z=-1.25 front=1
  part wheel r=0.31 w=0.2 x=0.74 z=1.3
  part box w=0.32 h=0.12 d=0.06 x=0.55 y=0.55 z=-2.04 color=#fff6d0 emit=1 mirror=1   # headlights
  part box w=0.3 h=0.1 d=0.06 x=0.6 y=0.6 z=2.04 color=#ff2010 emit=0.9 mirror=1      # tail lights
```

Every part takes `x y z` (its place), `rot` (degrees about the up axis), `pitch`, `roll`, `color`, `emit` (0..1),
`noise` (facet brightness variation, default 0.06) and `mirror=1` (also built mirrored at -x; wheels, wings,
engines and guns with x ≠ 0 mirror by default).

| part | values |
|---|---|
| `body` | `len wid hgt round=4 nose=0.2 tail=0.15 drop=0.35 pinch=0.12 seg=12` — a car body lofted along z: a rounded box (`round` 2 = oval, 6+ = boxy); over the first `nose` share of its length the top drops by `drop` (a sloping bonnet), over the last `tail` share likewise; `pinch` narrows the ends |
| `cabin` | `len wid hgt front=0.35 back=0.3 top=0.85 glass=colour` — the greenhouse: a prism whose roof is shorter than its base by the `front`/`back` shares (sloped windscreen and rear window) and narrower by `top`; the windows are `glass`, the roof `color` |
| `wheel` | `r w front=0/1` — a wheel (tyre and hub, `hub=colour`); front wheels steer in the picture; all wheels spin. Not part of the body mesh |
| `hull` | `len wid hgt shape=dart/cigar/wedge/blob/box/needle/tear/hammer/arrow/hex seg round belly` — a body lofted along z |
| `bar` | `from="x,y,z" to="x,y,z" r=0.04 seg=6` — a tube between two points (roll bars, aerials, push bars) |
| `box` | `w h d taper` (taper: top smaller) |
| `wing` | `span root tip sweep thick dihedral stripe` — from x=0 outwards (spoilers: small span, `pitch`) |
| `fin` | `h root tip sweep thick` — a vertical plate |
| `engine` | `r len r2 glow=colour seg` — a cylinder along z with a glowing end at +z (exhausts) |
| `gun` | `r len` — a barrel along z |
| `cone`, `spike` | `r len r2` — a cone, tip forwards (-z) |
| `disc` | `r h seg` — a flat cylinder about the up axis |
| `dome` | `r rx ry rz` — a half ellipsoid on top |
| `sphere`/`ball` | `r rx ry rz seg` |
| `light` | `r` — a small glowing ball |
| `ring` | `r thick seg` — a torus about the up axis |
| `crystal` | `r len seg` — a double pyramid along z |
| `plate` | `pts="x,z;x,z;..." thick` — a flat outline |
| `greeble` | `n w d size seed` — random small boxes on an area |
| `crowd` | `n w d seated fill mourners umbrella flags team="#c,#c,.." seed` — a row of spectators along x over `w` metres, facing -z, standing on y = 0 (the seated ones on a bench 0.42 m high just behind): `n` seats, `fill` of them taken, `seated` of the people sitting, `mourners` of them in black (top hats, veils, black umbrellas), `umbrella` and `flags` the shares holding one, fans in the `team` colours (shirts, scarves, caps) or casual clothes; about 75 triangles a person, every vertex marked for the shader's crowd (see "Life on the stands, wind"). Not pitched or rolled |
| `banner` | `w h ad=sponsor\|slogan` — a board the renderer prints on, lying in the x-z plane (pitch it onto a roof), the text's top towards -z: the player's sponsor (Graves & Sons when there is none; every other stand Graves & Sons' slogan) or the slogan. No facets of its own |

Car colours: `paint` (main), `paint2` (second), `paint3` (trim) are replaced by the driver's colours (the player
picks theirs; rivals have their own). A car model should use `paint` on most of the body, `paint2` on stripes,
bonnet or roof, and `paint3` sparingly. Fixed colours (chrome, rubber, lights, glass, rust) stay as written.

Weapons are drawn by the engine (gun barrels on the bonnet, mine rack at the back, rocket pods on the roof) at the
car's `mount_front`, `mount_roof` and `mount_rear` points (see Cars).

Every car needs a **wreck** look: the engine blackens and dents the model itself; no separate model is needed.

Props use the same parts. Sizes: a gravestone ~1 m high, a crypt 4–6 m, a tree 6–12 m, a container 12 x 2.4 x
2.6 m, a street lamp 6 m. Props that carry a lamp mark it with a `light` part; the engine adds a real light there
at night when the prop has `lamp=1` in its model head line (`model lamp_old lamp=1`).

`GravePrix --gallery work/gallery.png [prefix]` draws every model (or those whose name starts with prefix) on a
sheet.

## Cars (`data/cars/*.txt`)

```
car rustbucket
  title "Rustbucket"
  desc "..." desc_en "..."
  model car_rustbucket
  class 1               # 1 scrap .. 5 elite; 6 boss-only
  price 0               # 0: the starter car; sold for half of what was paid
  league 5              # on sale from this league on (5 lowest .. 1)
  mass 1050             # kg (heavier pushes harder and is pushed less)
  power 11              # m/s² of push at standstill with the engine at level 0
  top 36                # top speed m/s with the engine at level 0
  grip 1.0              # side grip (1 = an ordinary car on asphalt)
  brake 1.0
  steer 1.0             # how quickly it turns
  offroad 1.0           # grip off the road and on dirt, sand, snow (rally cars > 1)
  armor 100             # hit points with armour at level 0
  length 4.1 width 1.72 # its box for collisions
  engine i4             # its engine sound: tin i4 boxer v6 v8 v12 diesel rotary turbine electric
  horn plain            # plain truck dixie ice funeral siren klaxon
  gun mg                # its front gun (data/guns.txt)
  mount_front 0 0.9 -1.6   # x y z of the gun on the model
  mount_roof 0 1.4 0.3
  mount_rear 0 0.6 2.0
  upgrades engine=4 tires=4 armor=4 gun=4    # how many levels this car can take (1..6)
  upgrade_cost 1.0      # the price of its upgrades (x data/upgrades.txt)
  perk regen=0.5        # optional, see below
```

Perks (one or two per car, optional): `regen=hp/s` (repairs itself slowly), `bounty=x` (wrecks it causes pay x
times), `scare=1` (computer drivers make way when it comes from behind), `ramproof=x` (takes x of the ram damage),
`ammo=x` (gun ammo times x), `nitro=n` (+n nitro canisters each race), `mines=n` (+n free mines each race),
`slick=1` (its oil lasts twice as long).

Car classes and roughly how they compare (level-0 values; upgrades add up to ~+35 % power, +12 % top, +25 % grip,
+100 % armour):

| class | price | power | top | armour |
|---|---|---|---|---|
| 1 scrap | 0–6 k | 9–12 | 33–38 | 80–140 |
| 2 street | 8–20 k | 11–14 | 37–42 | 100–180 |
| 3 muscle | 25–45 k | 13–16 | 41–47 | 120–220 |
| 4 pro | 55–90 k | 15–18 | 45–52 | 140–260 |
| 5 elite | 110–180 k | 17–21 | 50–58 | 160–320 |
| 6 boss | — | 20 | 56 | 600 |

## Guns, gear and upgrades

`data/guns.txt`:

```
gun mg
  title "Bordkanone" title_en "Chain Gun"
  desc "..." desc_en "..."
  price 0               # 0: fitted to cars; else bought in the garage and fitted instead
  damage 3..7           # per bullet, from upgrade level 0 to the top level
  rate 9..15            # bullets a second
  ammo 150..320         # per race
  speed 170             # bullet speed m/s
  spread 1.6            # degrees
  range 80              # metres
  heat 0                # >0: no ammo, it overheats instead (flamer)
  kind bullet           # bullet | flame | shell
  sound mg
  color #ffd070
```

`data/gear.txt` — the black market's goods:

```
gear mine
  title "Tretmine" title_en "Landmine"
  desc ...
  kind mine             # mine rocket spikes oil smoke nitro ram plating
  price 150             # per pack
  pack 3                # how many a pack holds
  max 9                 # most a car can carry
  damage 45 radius=4 arm=0.6 life=90     # kind-specific values
```

Kinds: `mine` (dropped behind, arms after `arm` s, bursts on contact: `damage radius`), `rocket` (fired forward,
seeks the car ahead within `cone` degrees: `damage radius speed turn`), `spikes` (a strip behind: tyres burst,
grip x0.4 for `slow` s, `damage`), `oil` (a slick behind: no grip for ~0.7 s while on it, `life`), `smoke` (a cloud
behind: computer drivers in it steer badly, `life`), `nitro` (a canister: `boost` m/s² for `dur` s), `ram` (a
permanent plate per car: ram damage x`mult`, own ram damage x`self`), `plating` (permanent per car: +`hp`).

`data/upgrades.txt`:

```
upgrade engine
  title "Motor" title_en "Engine"
  cost 600 1400 2800 5200 9000 15000    # per level, times the car's upgrade_cost
  power 0.07 top 0.025                  # added per level (shares)
upgrade tires   ... grip 0.05 brake 0.05 offroad 0.05
upgrade armor   ... hp 0.18
upgrade gun     ... (moves the gun along its ranges)
```

Repairs cost money in proportion to the damage and the car's price (data/career.txt).

## Venues (`data/venues.txt`)

A venue is a place with its own look, sound and props; each has several tracks.

| key | place | league | look |
|---|---|---|---|
| `junkyard` | a scrap yard with car crushers and piles of wrecks | 5 | overcast day, rust, mud, gravel |
| `cemetery` | an old cemetery and its chapel district | 5 | night, fog, gravestones, crypts, cypresses |
| `docks` | a harbour with containers and cranes | 4 | dusk, rain, wet asphalt, water at the edge |
| `desert` | a desert town with a motel and a dry riverbed | 4 | glaring noon, sand, cacti, bleached bones |
| `refinery` | an oil refinery with flare stacks | 3 | night, flames, pipes, tanks, oil puddles |
| `swamp` | a bayou with stilt huts and sunken cars | 3 | humid dawn, mud, planks, shallow water |
| `downtown` | a city centre at night | 2 | night, neon, rain, wet streets, traffic lights |
| `pass` | a mountain pass in winter | 2 | snow, ice, fir trees, avalanche fences |
| `fairground` | an abandoned amusement park | 1 | dusk, rides, clowns, coloured bulbs |
| `proving` | a military proving ground | 1 | day, dust, craters, bunkers, tank traps |
| `estate` | Graves' estate: mausoleums, a private chapel, a gothic manor | final | thunderstorm night |

```
venue cemetery
  title "Friedhof Ewige Ruh" title_en "Eternal Rest Cemetery"
  desc "..." desc_en "..."
  league 5
  time night                 # day dusk night dawn noon overcast
  weather fog                # clear rain snow fog dust storm
  sun dir=40 height=35 color=#8090c0 power=0.35      # direction (degrees), elevation, colour, strength
  ambient #202838 power=0.6
  fog #1a2030 density=0.012
  grade shadows=#8aa0c0 highlights=#e6f0ff amount=0.8 sat=0.85 contrast=1.03 vignette=0.8   # the picture's colour
  ground grass color=#2c3a24 color2=#3a3020          # what lies beside the road (grass dirt sand snow mud concrete gravel water)
  road asphalt color=#3a3a40 line=#c8c8b0            # the default road surface and its marking colour
  shoulder gravel color=#4a4438                      # the strip between road and wall
  wall stone                 # the default wall style
  props gravestone*8 cypress*2 crypt angel lamp_old  # props scattered beside the track (*n: weight)
  density 1.0                # how many of them
  litter flowers*4 wreath gravestone*2   # small things in a band 1-9 m beyond the road's edge
  music cemetery             # assets/music/<name>.ogg (several pieces: cemetery, cemetery_2 ...)
  ambience cemetery          # the background loop (Audio)
```

Surfaces (road, shoulder, ground and patches): `asphalt concrete cobble dirt gravel sand mud snow ice metal wood
water grass`. Grip and drag: asphalt 1.0, concrete 0.97, cobble 0.9, metal 0.85, wood 0.9, dirt 0.8, grass 0.72,
gravel 0.72, water 0.7 (and drag), snow 0.6, sand 0.65 (drag), mud 0.55 (drag), ice 0.35.

Wall styles: `concrete` (jersey barriers), `tires` (stacked tyres), `rail` (steel guardrail), `stone` (old stone
wall), `iron` (wrought-iron fence on a plinth), `fence` (chain-link), `wood` (planks), `containers`, `sandbags`,
`snowbank`, `pipes`, `hedge`, `scrap` (piled wrecks), `neon` (city barriers with light strips), `none` (no wall:
only for crossings; the edge is still solid).

## Tracks (`data/tracks/<venue>.txt`)

A track is a closed loop described by a **turtle path**: from the start line, straights and arcs, then `close`
joins the end smoothly back to the start.

```
track cem_chapel
  title "Kapellenkurve" title_en "Chapel Bend"
  venue cemetery
  laps 4                     # 3..6
  width 13                   # road width in metres (11..18)
  shoulder 3                 # the strip each side between road and wall (2..6)
  start 0 0 90               # x z heading: the start line and the direction of travel
  straight 90
  right 90 r=35              # an arc to the right through 90 degrees on radius 35 (radius of the centre line)
  straight 40 w=16           # the width changes to 16 over this piece
  left 120 r=22 surface=cobble   # the road surface of this piece
  straight 60 wall=tires     # the wall style along this piece
  right 30 r=80
  close                      # joins back to the start (keep the gap and the turn small: let the path come
                             #   back near the start, heading roughly the start's way)
```

Path pieces: `straight L`, `left DEG r=R`, `right DEG r=R`. Values on a piece: `w=` (width at its end, blended
from the width before), `surface=` (road surface of the piece), `wall=` (wall style), `wall_left=` / `wall_right=`
(the style of one side only), `shoulder=`.

Things along the track: `at=` is the distance along the centre line from the start line (metres); `lat=` is the
side position as a share of the half width (-1 the left edge, 0 the middle, 1 the right edge, looking the way of
travel). Outside the road, `side=left|right|both` and `off=` metres beyond the wall.

```
  pickup cash at=120 lat=0.4        # cash wrench ammo nitro mine rocket mystery
  hazard oil at=300 lat=-0.3 r=3    # oil (a slick) barrel (explodes when shot or hit) mine (a live mine)
  obstacle pillar at=410 lat=0.5 r=1.2 model=pillar_stone   # a solid round thing on the road
  patch ice at=500 len=40 lat=0 w=10    # a patch of another surface on the road (any surface; the AI slows for it
                                         #   on its line and steers round oil and ice when there is room)
  prop crypt at=200 side=right off=6 rot=90    # one prop beside the track
  row gravestone at=50 len=120 side=both off=2 every=3   # props in a row along the wall
  place crane x=40 z=-120 rot=30    # a prop at a point of the map (x z in metres, the start is 0 0)
  lamps lamp_old every=45 side=both   # lamps along the track (lit at night)
  bridge at=600 len=60              # the road runs over a bridge here (drawn so; for figure-eight tracks)
```

- Every track gets scattered props from its venue automatically (`density 0` on the track turns that off).
- Pickups: without `pickup` lines the engine places ~1 every 120 m. Picked-up items come back after 20 s.
- **Crossings** (figure eights) are allowed where the path crosses itself at more than 35°; mark one of the two
  passes with `bridge` (drawn as a bridge; still a flat crossing for the cars). Elsewhere two parts of the track must
  stay apart: centre lines at least (width + 2 shoulders + 6 m) apart.
- `reverse 1` in a track's block lets the career offer it the other way round as well; `mirror 1` mirrored.
- Lengths 600–1800 m; tight hairpins r ≥ 12, ordinary corners r 20–60, sweepers r 80+.

The track's clockwork (`src/Movers.cpp`, all by the race clock, so a race replays the same way):

```
  train at=374 period=40 offset=12          # a level crossing: 5 s of bells, then a goods train across the road
  drawbridge at=695 len=24 period=50        # a bascule bridge: bells, 3 s up, 8 s up, 3 s down (a wall while up)
  rockfall at=300 len=120 period=10         # a rock falls somewhere on these 120 m every 10 s, lies there 12 s
  weather rain lap=2                        # rain (grip -16 % on hard roads) or fog comes when the leader starts lap 2
```

The AI stops short of a train or a raised bridge, goes through when it is already too close to stop, and steers
round rocks. Random tracks (`src/RandomTrack.cpp`): `randomTrack( venue, seed )` turns dice into straights and
corners that close with `close`, checks them like a hand-made track (both directions, no warnings) and registers
"rnd_<venue>_<seed>" outside the track order; the tools take such names (`--sim rnd_cemetery_7`).

Check a track with `GravePrix --trackshot <name> work/maps/<name>.png` (a map: road, walls, start, numbers every
100 m, pickups and props, warnings drawn in red) and `GravePrix --sim <name>` (six computer drivers race it; prints
lap times and where cars got stuck). `--validate` must report 0 errors for every track.

## Driving and the assist

The arcade model (13.11.2026, the user: "Die Kurvensteuerung ist immer noch unterirdisch"; `World::physics`, the
constants at the top of Sim.cpp). Until 1.0.1 the nose turned no faster than the grip carried (1.4 x grip x 15 m/s²
/ speed with the assist): at 108 km/h the first car turned on a 39 m radius, nine car lengths, and every track had a
bend that asked for the brake down to 36-76 % of top speed long before it. Top-down racers of the kind turn on two
to four car lengths. Now, for every car, the player's and the computer's:

- **The nose** turns as fast as the steering asks at any speed: full lock is 2.4 rad/s, a third less at full speed
  (`steerShare`), times the car's `steer` with half its spread (`SteerSpread`: 1.22 turns 11 % quicker, 0.8 10 %
  slower; at speed it now turns the nose too, the reaper's taps were too coarse for a keyboard). `fullLock`. Nothing
  holds the nose to the grip any more.
- **The tyres** swing the path round to the nose: they take the slip (the speed across the nose) away at `TyreBite`
  per second, at most with `TyreHold` x 15 m/s² x the car's grip x the ground's grip (`sideGrip`). A turn within the
  grip leaves a small slip that settles at once; beyond it the car slides. The nose swings back towards the path by
  itself (`TyreAlign` per second per radian of slide) unless the steering holds it, as a real car's front wheels
  straighten a slide: let go of the key and the slide is over (`--handling`'s "settle"). The straightening is what
  a keyboard needs: every tap leaves a slip, and the hands of a person (`World::hands`) lap the final's hedge maze
  6.8 % quicker with 5 than with 3.5 and 20 % slower with 2.
- **Scrub:** steering held beyond the turn the tyres carry at this speed (hold / speed) slows the car, as a real car
  ploughs: 1 m/s² per m/s² of the excess, at most as hard as the tyres hold, and the drive slips by the same share.
  A bend taken too fast costs speed in the bend instead of the wall or the brake long before it. What counts is the
  steering held over a quarter of a second (`Car::m_lock`), so short taps steer without scrubbing and a held key
  scrubs (measured on the wheels of a tenth of a second ago, the tapping hands of the career's test driver lost the
  final against Graves for ever: two Blechschaden careers of six, 190 finals each). On ice the tyres scrub no harder
  than they grip: the car slides on.
- The handbrake: the rear holds 35 %, the nose turns half again as fast, no scrub, nothing straightens: the drift.
  Oil (grip x 0.18), spikes (x 0.55), the surfaces, rain and the engine upgrades' `slip` scale hold and bite as
  before; reversing (the brake held at a standstill) and the reversing lights are unchanged.

| rustbucket, stock (`--handling`) | 1.0.1 assist on | assist on | assist off |
|---|---|---|---|
| U-turn at full throttle from 36 / 72 / 108 km/h (width) | 24.5 / 33.8 / 39.1 m | 16.0 / 19.6 / 21.7 m | 13.9 / 16.4 / 17.8 m |
| full lock held 5 s at full throttle: circle, speed | 2.0 m at 24 km/h | 7.5 m at 45 km/h | 6.1 m at 38 km/h |
| a 0.1 s tap at 90 km/h turns the car | 5.1 deg | 8.2 deg | 8.6 deg |
| slide gone (< 1 m/s) after letting go of 1.5 s of full lock (1.0.1 off: 0.95 s) | 0.15 s | 0.08 s | 0.18 s |
| half a second of handbrake and full lock from 72 km/h | | car 70 deg, path 5 deg | car 70 deg, path 4 deg |
| two seconds of full lock on ice from 72 km/h | | car 90 deg, path 46 deg | car 114 deg, path 50 deg |

Bigger, stronger cars carry more speed round a held full lock (the mausoleum at engine level 4: a 13 m circle at
71 km/h, its U-turn from 108 km/h 33 m wide). `GravePrix --handling [--car NAME] [--level N] [--wall]` prints the
table (reversing, 90 degrees, U-turns and the circle at each speed, the settling slide, the handbrake, ice and a tap,
with and without the assist; `--wall` drives into a wall and backs out); `--handling --tracks` lists every race
track's slowest bend for that car (the speed its tyres carry on the racing line's tightest curve against its top
speed) and the bends' radii (10 % 15 m, median 40 m: the first car's slowest bends carry 38-79 % of top, median 53 %,
with the assist: the hold is about the old one, a too fast entry scrubs instead).

**The assist** (`Settings::m_assist`, `CarSetup::m_assist`), three levels, the options cycle them:

- **0 off**, for experts: hold 1.3, bite 10/s, align 2.5: the tyres every computer driver has; the car slides
  further and longer and holds less. A career race won so earns "Wie früher" (`old_school`).
- **1 on** (the default): hold 1.5, bite 16/s, align 5: grippy, a slide settles as soon as the key is let go.
- **2 on with the brake help** (`World::brakeHelp`): as 1, and the car lifts the throttle when it is faster than the
  bends ahead allow and brakes when it is more than 1 m/s faster (as hard as the excess / 5 m/s, at least a quarter):
  the computer drivers' plan for the bends (`World::cornerSpeed`: the racing line's curvature against the side grip
  on the ground there, with room to brake) at 95 % of the grip. It only ever takes away (never more throttle than the
  driver gives, never less brake) and keeps out of reversing, the handbrake, derbies and a car across the road. A
  driver who never lifts for a bend (`--flatout`, with a person's hands) hit the walls 3763 times over all tracks
  with the assist and 1365 times with the brake help (28.8 against 30.2 m/s; a careful driver 1252 times at 30.2).

Settings files from before the levels (`assist 1` / `assist 0`) read as on / off. `--careersim --assist 0|1|2`,
`--sim --assist [LEVEL]` (1 without a level), `tools/careersweep.py --assist`.

**The computer drivers** steer by the arc from where the car is going (its path, not its nose) through the point
ahead: the turn it asks over `fullLock` is the share of lock, 1.3 times that for the tyres' slip and the
straightening, the nose's swing damped (holding the lock to what the tyres carry cost 4 % of the pace and half again
as many wall hits: the line needs its corrections). Their bends are planned with `sideGrip` (`World::cornerSpeed`).
In a derby they hunt with no more lock than the tyres carry, unless the target is far round (scrubbing would cost the
ram its speed). The obstacles (posts, tank traps, rocks, barrels) collide with the car's whole box, not eight points
of it: a tank trap between the points held cars on prv_shellshock for good (324 times in one race of the field).

Measured over all 45 race tracks (`--simall --peaceful`, 1.0.1 against now): the computer field 30.25 -> 31.67 m/s,
stuck 372 -> 26 (48 without prv_shellshock's tank trap), wall hits 1533 -> 1430; a person's hands with the assist
(`--assist --hands 0.3`) 28.54 -> 30.22 m/s, stuck 48 -> 26, wall hits 1443 -> 1252; the clean bot with the assist
30.80 -> 32.84 m/s, wall hits 1155 -> 1209. So the hands keep about their distance to the field (94.3 -> 95.4 %), the
clean bot (a pad) gains 2 %. Derbies last longer (18 armed: 2257 -> 2858 s, 84 wrecks both): the grippier cars get
away from a ram more often. The career (`tools/careersweep.py`, the practised hands 0.3 and the clean bot, the same
seeds as for 1.0.1): Schonfrist 3/3 won in 56-79 days (53-83), Blechschaden six seeds 6/6 in 80-151 days (83-120, the
mean 104 both), Totalschaden 0/3 (bankrupt in league 3; before stopped in leagues 3-4); the clean bot wins d0-d2 3/3
(29-52 days), over six seeds d3 5/6 and d4 4/6 (1.0.1: 6/6 and 3/6). Graves at the final drives at 0.89 instead of
0.9: the car the test driver mostly reaches him in, the reaper with every upgrade (`--careersim --final reaper --hands
0.3`, Blechschaden, eight seeds), needed 7-16 finals with 0.88, 17-39 with 0.9 and 10-27 with 1.0.1 (two seeds of
eight more than 40 each).

## Balance: a person's hands, steeper leagues, upgrades with a price

`CarSetup::m_hands` (0..1) makes a computer driver drive like a person at a keyboard (`World::hands`): its inputs come
late (0.07 s practised .. 0.2 s a beginner), the steering keys know only full lock or nothing (tapped out in tenths
of a second, a little too much), the pedals only all or nothing. `--careersim --hands 0.3` (a practised player),
`--sim --hands 1`, `tools/careersweep.py --hands`. With such hands the slide was 2.7 m/s without the assist and 0.5
with it; the clean bot (no hands) hardly needs it.

The leagues get steeper (`data/leagues.txt` `pace` and `skill`: the rivals' engines and driving): league 5 is gentle
(0.86 / 0.78), league 1 harsher (0.96 / 0.95), Graves at the final 0.89 / 0.9 (0.9 / 0.9 before the arcade model,
"Driving and the assist"); Sargnagel and Totentanz drive 1.5 % faster to make up for it. Measured with the practised hands (0.3, three seeds): Schonfrist won in 77 to 143 days,
the places rising from about 2 in league 5 to about 3 in league 1; Blechschaden reaches league 1 (place 3.5 to 4
there with the 3.3 t mausoleum the bot buys); Totalschaden reaches leagues 3 and 2; Sargnagel and Totentanz are for
the clean bot. The career bot is no judge of cars: a person would pick a lighter one. And every upgrade has its price on the road (`data/upgrades.txt`):
`slip` (the engine: the rear's grip lost per level under full throttle at speed; the computer drivers corner with
half of it in hand), `drag` (the tyres: top speed lost per level), `mass` (the armour: weight per level, power and
braking scaled by the lighter car's share, harder in crashes). The garage shows the next level's numbers.

## Loose things and life by the road

Venues name `loose` models (bins, cones, crates, sawhorses, grave crosses, snowmen, gnomes, hydrants ...,
`data/models/props_loose.txt`): `Track::loose` puts one on a shoulder now and then (inside the walls, never at the
start, the clockwork, a pickup or an obstacle), sometimes two or three together. The race moves them (`World::m_loose`):
a car or a bullet or a blast knocks them flying, they tip over (tumbleweeds and tyres roll, snowmen burst, hydrants
break and spout for a while), slide to a stop and stay; the car loses 1.5 % of its speed. `EvE::LOOSE` tells the
sound and the pictures.

The estate got its garden (`tools/props_estate_v2.py`, eight more scenes): a hedge maze, the pool with a hearse gone
in nose first, a greenhouse, a gazebo, a weeping angel, topiary skulls and an army of garden gnomes.

Life by the road is only pictures (`Renderer::life`): people near the road (`fig_*`, `crowd_*` within 22 m) are not
baked: a fast car close by and one throws themself flat (crowds step back and hop); flocks of the venue's birds
(crows, gulls, pigeons, vultures, the estate's bats) sit beyond the shoulders and fly up when a car comes, back when
it is quiet; a dog at each kennel (and a black one at the estate's gates) runs along with the field as far as its
chain lets it, barking. `Renderer::m_cues` asks the race for the sounds (`SfxE::BIRDS_UP`, `SfxE::DOG_BARK`).

The people themselves are skinned figures (`src/People.cpp`, `src/Skin.cpp` from DARK CONCORDANCE): Quaternius' CC0
base bodies, decimated by `tools/make_people3d.py` (Blender for the decimation, the rig and the weights kept) to about
2,500 triangles for the roadside, the marshals and the fans and 7,800 for the podium, with the animation libraries cut
to the clips played (`assets/models/people3d`). Outfits are vertex colours bone by bone (coat, trousers, shoes, hands,
hair, a shirt and tie, reflective stripes), hats and held things are Forge models on the head bone and in the fist
(top hats, caps, a hard hat, the yellow flag, Ricky's microphone, the widow's umbrella). The acts are the library's
clips with the arms set by IK on top: idle (some fold their arms), cheer, clap, wave, the flag swung overhead, a
microphone at the mouth, and the dive aside (Death01, then LayToIdle). Skinned on the CPU into a mesh per figure (once
a frame, the shadow pass reuses it), so the lit and the depth shaders, the contact shadows and the fog need nothing
new. `fig_*` become an outfit each; the `crowd_*` groups are read for their people (head, torso and legs: where, which
colours) and drawn as fans or mourners within 50 m of the view, at most 24 skinned figures a picture (the rest stay the
Forge's); their clock ticks 30 times a second, each figure on its own beat, so each is skinned every other picture. The podium has Graves clapping, Ricky waving with the microphone and talking into it, and six fans; the
marshals beside a wreck swing the yellow flag. `--oldpeople` draws the Forge's figures instead (for comparisons),
`--podium FILE` pictures the podium, `--shot ... --near MODEL [N]` looks at a prop by the road.

## The walls, and how they give

The showdown asks the league's points (40) in the first season, six fewer in every further season spent in the
league, down to 22 (`Career::showdownPoints`), and the place it asks widens from second to third after two seasons and
fourth after four (`Career::showdownRank`): a mid-field driver is not held for ever, the duel with the champion stays
the gate. Every final Graves wins costs him two per cent of his pace, at most twelve (`Career::m_finalsLost`, his
rheumatism).

## Career slots and the credits

Three careers (`career.txt`, `career2.txt`, `career3.txt` in the player's folder; the first is the old single one).
"Continue" goes straight on with the only one, else shows them; "New career" takes the first slot while there is
none, else asks where (a taken slot is overwritten when chosen twice). The credits roll (`App::credits`) from the
title and after Graves is beaten (the ending, the credits, the table): the people of Graves & Sons, the whole field
(this career's dead marked, and in memoriam with their last words), the tools, the licence.

## Graves' jobs (`JobE`, `Career::jobDone`)

Every other race day or so a fourth card: a job for Graves on a track of the league, half the purse for places, no
league points, paid apart (`Offer::m_pay`: 0.8 to 1.1 of the purse). Coffin: a coffin on the roof (`Renderer::
m_roofCoffin`), the finish with at most 40 % of the armour lost, else it falls off. Debtor: a rival of the field
(`Offer::m_target`) must be wrecked by the player. Escort: Graves' hearse (`CarSetup::m_escort`, the pallbearer, slow
and unarmed) must finish; the rivals ram it first and shoot at it from a wider angle. Wreaths: nine `wreath` pickups
on the racing line (back after 15 s); six to pick up. The tabloid reports either way (`job_done_<key>`,
`job_failed_<key>`), Graves says why at the start (`job_<key>`), Ricky when it is decided. The career bot leaves
them alone unless `--jobs`.

## Epitaphs and the crypt (`data/awards.txt`, `AppAwards.cpp`)

65 epitaphs over all races and careers (`Records::m_awards`, with counters `m_count` and the venues and tracks raced
`m_seen`); the code checks them by key after every race step (events), at a race's end and after a career race (the
report, the rivals, the tabloid), and in the shops. A new one is a toast and a stamp; the crypt shows them as
gravestones (title screen and paddock). The app tests earn none.

## The test drive (`App::startTutorial`)

Eight steps on `test_undertaker` (Graves & Sons' yard), a time trial alone: throttle and steering, braking, the
handbrake in a hairpin, shooting three barrels, a mine, nitro, a rocket, the line. Ricky explains each (`tut_1` ..
`tut_8`, `tut_done`), a panel shows the step and its key. Passing it earns the licence (`Records::m_tutorial`). It
heads the paddock's list on a career's first day and is on the title screen.

## The driving school (`AppSchool.cpp`)

From the options' driving tab, a screen of its own (`ScreenE::SCHOOL`): three exercises on the proving ground
(`data/tracks/school.txt`, `test_` names: no career, quick race or menu backdrop takes them), one lap each, alone, a
Rustbucket with engine, tyres and armour at level 2 and no weapons, the player's assist; the racing line is on and
the game speed is ignored (`App::m_schoolOn`). A career is set aside meanwhile (`m_schoolCareer`).

- **Slalom** (`test_school_slalom`): nine gates on the first straight, 30 m apart, their middles at 0.45 of the half
  width left and right in turn, 4.6 m wide. A track line `gate at= lat= w=` makes a `GateSpot` and two cones in
  `Track::m_loose` (they fly when hit). A gate counts as passed when the car's distance reaches it, by its offset from
  the centre then (`App::schoolStep`); a missed gate costs 2 s, a cone knocked over (`EvE::LOOSE`, "cone") 1 s; gates
  not reached at the finish count as missed.
- **Hairpins** (`test_school_hairpin`): two 260 m straights joined by hairpins of 12.5 m.
- **Esses** (`test_school_esses`): two strings of left 60°, right 120°, left 60° (radius 30, then 20).

`school bronze= silver= gold=` gives the medals' times (seconds with the penalties), set from the computer drivers'
best run on a free road (`--sim TRACK --laps 1 --car rustbucket`: 28.3, 22.6, 37.1 s) with a little for weaving: gold
about 6 % above it, silver 12-15 %, bronze 28-30 %. The best time per exercise goes into `Records::m_bestLap`, the
best medal into `Records::m_count["school_<track>"]`; three golds earn `school_gold`. A time trial's ghost (the best
lap) runs along. App test word `school N` starts exercise N.

## The racing line, the tyres' warning, the game speed

**The racing line** (option "Ideallinie", off by default; always on in the driving school): `World::speedPlan` gives
every sample of the lap the speed the car may have there: the racing line's curvature against 0.95 of its tyres' side
grip on the ground there (and the rain), braked back from the next bend at its brakes on that ground (two passes
round the lap), as the computer drivers' `cornerSpeed` plans it. `App::racingLine` puts points every 2 m from 4 m
ahead of the nose to 4 s at the speed (60-170 m), each coloured by the car's speed now against the plan there: 1 m/s
under it and more green, up to 1 m/s over yellow, 3 m/s over and more red (the plan brakes in time, so red starts
where the braking must start). The renderer (`Renderer::m_guide`, `drawDecals`) draws a soft band 1.4 m wide under
the cars, fading in near the car and out towards its end; the red-green palette draws blue, pale yellow and orange.

**The tyres' warning** (option "Reifen warnen", on): the physics keeps `Car::m_gripUse`, the larger of the slip's
share of the grip (it squeals from 0.9, so divided by 0.9) and the turn the steering asks against what the tyres carry
(it scrubs from 1), smoothed over a tenth of a second. From 0.78 to 0.97 a thin whine fades in (`LoopE::TYRE_SING`,
1640 Hz, higher as it rises) and the pad hums a little; above 1 the squeal takes over. Not off the road.

**Game speed** (option "Spieltempo", 70-100 % in steps of 5): the race steps slower (`App::race` feeds the steps
`dt x speed`; the renderer's effects too), the screen and the camera keep their pace. Best laps and ghosts are kept
only at 100 %.

## The options' tabs

`App::options` builds its rows per tab (Bild, Ton, Anzeige, Fahren; Q/E, PageUp/PageDown, LB/RB or the mouse on the
tab's name): a text, what left/right or accept does to it, a line of help shown in a panel beside the list, and
whether the renderer, the sound or the text follow (`applySettings`). Buttons: the driving school, the controls, the
benchmark; the last row of every tab goes back. The tab stays when the controls are left (`m_optTab`).

## The replay

`App::raceStep` keeps the last eight seconds of the race (480 whole `World` steps in a ring, 15-25 MB) while the option
"Wiederholung im Ziel" is on (not in the school or the test drive); 1.5 s after a human's finish or wreck (in a derby
after any wreck) it stops, so a long run-out cannot push the moment out, and a later moment starts it again. When the race ends `App::startReplay` looks for the
moment: a human's finish, else a human's wreck, else the winner's finish, in a derby the last wreck; the clip runs
from 2.5 s before it to 1.5 s after. `App::replay` plays it at 0.35 of the speed: each step's events go to the
renderer once (the sparks, the confetti and the fireworks come again), three cameras at the track cut over it (one
waiting where the moment happens at 30 m, one low behind the car at 20 m, one wide above the line rising from 36 m),
bars at the top and the bottom, "WIEDERHOLUNG" with a blinking red dot, who it is; the applause or a slowed-down
explosion at the moment. Enter, Esc or a click skip it. With two players it fills the screen. The app tests skip it
unless the script says `replayon`.

## Two players on one screen

The quick race's row "Spieler: 1/2" (races and derbies; the second player needs a pad: with one pad the first player
keeps the keys, with two the first takes the first pad and the keys, `Input::route`). The second human is the same car
in another paint (`scheme( colour + 5 )`) beside the first on the grid (`App::m_player2`). Two renderers half the
screen wide each (`App::splitViews`; `applySettings` keeps them split while racing), two cameras (`App::leadCamera`
for each player: 12 % higher, the lead across the half 0.55 of the full one), each half its own display
(`App::splitHud`: place, lap and time, armour, ammunition, gear, speed, a word when finished or wrecked), the map, the
countdown and the announcer shared in the middle (`App::miniMap`). The pace notes, the shouts and the automatic
miniature look's watch stay with one player. Both engines are heard close; the sound's ear is the first player's
camera. App test word `split` (two players without a pad; `drive` gives both cars to the computer).

## Graves & Sons collect the wrecks

Not in a derby (its wrecks are the arena's furniture): `World::TowAfter` (10 s) after a car is wrecked
`Car::m_tow` starts; the wreck stays where it lies (`m_towAt`). After `World::TowLift` (3.5 s) it leaves the ground
(`m_gone`: its position far away, so nothing on the track meets it again; the computer drivers look past it). The
pictures (`Renderer::drawTow`, `drawCar`): the hook (`tow_hook`, a black block with the firm's brass plate) comes down
from above the picture on its cable in 2.5 s, sits on the roof, then rises with the wreck (1.8 t² + 0.6 t metres,
turning slowly) out of the picture. From the wreck until it is gone two marshals (`fig_marshal`, `flag_yellow`) stand
at the shoulder on its side, one beside it and one 30 m before it, waving yellow flags. The race says "Graves & Sons
holen X ab" in the feed. Cars on the track: in `--simall --difficulty 2` the computer drivers stuck 36 times instead
of 48, walls 365 instead of 394, 31.8 m/s instead of 31.4 (45 tracks). `--shot ... --tow SECONDS` wrecks the car
nearest to the view and shows the hook that far in.

## The crowd's voices

`Renderer::standCount/standAt/standReach` and `joy` tell the race where the stands are. `App::crowdSound`: the roar
of the nearest stand (`LoopE::CROWD`: noise through the vowels' formants, single shouts and finger whistles in it,
10 s stereo), by its distance from the camera, louder while the stands cheer after a finish; a cheer
(`SfxE::CHEER`) when a car faster than 58 km/h passes within 16 m of a stand near the camera (each stand at most every
7 s); a gasp (`SfxE::GASP`, many voices' "oooh") at a crash harder than 14 or a wreck within 40 m of a stand (at most
every 2 s); applause (`SfxE::APPLAUSE`) at the player's finish where there are stands. All synthesised (Audio.cpp:
`crowdShout`, `fingerWhistle`, `crowdRoar`, `claps`).

## The tabloid's photo

On the career's results screen, 1.4 s in (the confetti in the air; at the latest when the screen is left),
`App::takePhoto` copies the podium (3:2 round its middle), turns it into newsprint (grey, harder, a little grain, the paper's tint) and keeps it for the day. The tabloid's front
page shows it instead of an archive picture when its top story is that race's (a win, a podium, a showdown, a record,
a spree, a clean win, a loss, a rival's death or a feud's end).

## The benchmark and the automatic miniature look

The miniature look's setting has three values (`Settings::m_miniature`): off, on, automatic (the default). Automatic
watches the frame rate in a race (`App::watchFrameRate`, from 6 s in, one player): in windows of five seconds, when
in two in a row more than a quarter of the frames came later than 1.3 frames of the target (the frame limit, 60 when
unlimited), the look goes off for good (`Settings::m_lookDropped`, until the option is changed) and the feed says so.
The benchmark (picture tab, `ScreenE::BENCH`): `est_finale` (storm, rain, lightning, stands) with six computer
drivers, the frame limit lifted; 2 s to settle and 8 s measured with the look, the same without. The look stays when
the average holds 97 % of the target and the slowest 5 % of the frames 80 % (a hitch or two must not decide); else it goes (automatic, dropped), and
when even without it the frame rate does not hold the graphics go one step down.

## Seeing the road, and getting back on it

The user (13.11.2026): "Die Kurvensteuerung ist immer noch unterirdisch." One reason was that the bends came into
view too late, another that a pad's stick steers mirrored when the car drives down the screen.

**The race camera** (`App::raceCamera`): north up and never turning (turning makes people sick), its height
(58 + 0.55 x speed) x the "Kamerahöhe" option as before. It leads the car the way it goes: the lead follows the
velocity, damped (0.6 s, so a spin or a crash glides), whole from 54 km/h and less while crawling, a share of the
height: 0.18 up the screen, 0.465 down, 0.35 across. Up and down differ because the picture is tilted
(`Renderer::render`: the eye 0.36 of the height behind the focus, the target 0.06 ahead): from the focus it sees 0.65
of the height up the screen and 0.36 down. The car itself is followed without lag (the old camera trailed it by a
quarter of its speed); a jump (back on the track) glides. Metres from the car to the picture's edge the way it drives,
before → after (`tools/camera_view.py`: the camera's geometry, fovy 45, settled on a straight; 16:9 and 16:10
differ only across):

| speed | up | down | across 16:9 | across 16:10 |
|---|---|---|---|---|
| 54 km/h | 47 → 55 | 27 → 54 | 56 → 75 | 51 → 70 |
| 108 km/h | 56 → 62 | 34 → 61 | 66 → 84 | 60 → 78 |
| 180 km/h | 69 → 71 | 44 → 70 | 80 → 97 | 74 → 90 |

Driving down the screen the car sits 14 % below the top edge (up: 77 %, across: 25-28 % from the side it comes from).
So the messages in the middle of the display (the pace notes, the rivals' shouts) move to the bottom while the car is
high on the screen (above 36 % of the height, back above 50 %; they fade over) and never cover it.

**Pace notes** (`App::findBends`, `App::paceNotes`; the option "Kurvenpfeile", on by default; not in arenas): a panel
at the top of the display (under the job's or the test drive's panel), not boards in the world: it reads the same on
every ground, at night and in fog, and boards would first have to come into view, which was the trouble. The bends
are found once per race: where the centre line (its curvature smoothed over 14 m) bends one way with a radius under
250 m (a short straight between two arcs the same way belongs to the bend), its turn (the summed curvature) and the
racing line's tightest radius in it and 12 m around. Kinks (under 15 degrees and wider than 40 m) are left out, and so
are bends the player's car takes flat out (sqrt(18 x grip x radius) at least 97 % of its top speed). Shown: the next
bend within 140 m or 4.5 s, and the one after it when it begins less than 60 m after the first ends ("into"); each as
an arrow of its shape seen from above, north up like the picture (a stem the way in, an arc through its real turn, so
a hairpin is a U), the distance in 10 m steps, and its grade in colour and one to four marks: 1 a radius of 75 m and
more (Good), 2 40-75 m (Gold), 3 22-40 m (orange), 4 tighter (Warn; the colour-vision palettes change Good and Warn).
The HUD size option scales it.

**The stick points the way** (`Input::aimSteer`; the option "Lenkung: Stick dreht / Stick zeigt Richtung", the
default turns): with a pad the left stick's direction on the screen is where the nose should point. A controller on
the angle between the heading and that direction steers (gain 3.6 per radian, so full lock from about 16 degrees off,
damped by the car's own turn, 0.32 s), and the nose comes round without swinging past: `GravePrix --aim [--car NAME]
[--level N]` turns the stick by 30 to 180 degrees at 6 to 35 m/s with the throttle held and prints the seconds to
settle within 5 degrees, the swing past and the steering's changes of side (all 21 cars at level 4: no swing; 90
degrees at 15 m/s in 1.0-1.3 s, as quick as full lock). A round dead zone (0.3); a stick held straight back keeps
turning the way it began; backing up (the brake at a standstill) the steering is mirrored, so the nose still comes
round to the stick. Throttle and brake stay on the triggers; the keys, the cross and the computer drivers are not
touched. `App::raceStep` hands `Input::car` the player's car when the option is on.

**Back on the track** (`World::resetCar`; the action "Zurück auf die Strecke": Backspace or T, the pad's Select/View):
the car goes onto the racing line 8 m behind where it is along the lap (further back or beside the line when a car, a
wreck, an obstacle, a rock, a barrel or something dropped stands there; never onto a level crossing or a drawbridge),
facing the way of travel, standing. The price: the standstill and the metres back (the lap's distance goes back with
it, so nothing is gained), and the next reset waits 5 s (`World::ResetWait`); not in the countdown, not wrecked or
finished. For 2.5 s (`World::ResetGhost`, `Car::m_ghostUntil`) it passes through the other cars (`World::collide`
skips it) and blinks (`Renderer::drawCar`). In an arena it goes to the spot farthest from the others.
`GravePrix --resets` puts the first of six computer drivers back every 6 s on every track, both ways (2244 resets:
all inside the walls with room, facing along the track, at most 62 m back). When the player has stood still for 2.5 s
trying to drive, or driven the wrong way for 2 s, the display names the key. A pause ends with the presses it saw
forgotten (Backspace leaves the pause menu). App test words: `drive` (a computer driver takes the player's wheel at
the race's pace, for pictures in the middle of a race), `reset`.

## Ghosts and stickers

On the cemetery's tracks and at the final the last two rivals to die race along as ghosts (`App::spirits`): along
the racing line at what its bends allow, pale green, a mist behind them, harmless; near the player they say their
old lines or their last words again. A signed sponsor's name goes on the player's roof (on the bonnet when a coffin
or a rocket pod takes the roof): `Renderer::setSticker`, a texture drawn once from the interface font.

## Ricky about the player

Besides the start, the lead, the last lap and the end Ricky comments on the player's own deeds (`announcer_voice`):
`player_record` (the race's fastest lap), `player_close` (a win by less than 15 m), `player_comeback` (a win from
last place), `player_clean`, `player_spree` (the third wreck of a race), `player_revenge` (a feud settled),
`player_wreck` (now and then), the jobs (`job_*`), the ghosts (`ghost`) and the test drive; five takes of his most
frequent lines.

## Life on the stands, wind

The crowds of the stands and the wind move in the vertex shaders (the lit one and the shadow's: `AnimGlsl` in
Render.cpp), never per figure on the CPU and never with a draw call of their own. The Forge marks the vertices in their
texture coordinates (`MarkE` in Forge.h): uv.x = the kind + a share, uv.y = a length, uv2 = the anchor on the ground
(x, z) the vertex belongs to (a person, a tree, a flag's pole). The marks ride into the baked prop chunks
(`MeshBuilder::append` moves the anchors with the prop, scales the lengths and gives each stand its own number);
`Chunk::m_anim` turns the shader's `anim` on for chunks that carry marks (a mesh without texture coordinates reads
garbage there, so it stays off for everything else). Models without marks keep no texture coordinates.

- **The crowd** (`part crowd`): the fans sway and breathe; a fast car within ~45 m and they cheer (the eager ones
  first): the seated stand up, the standing hop, arms go up in a V (an arm vertex knows how far below the shoulder it
  hangs and mirrors above it). Now and then a wave (la ola) runs along a stand at 11 m/s, each stand on its own clock;
  mourners and people under umbrellas only sway. `crowdJoy` makes everyone cheer: after a finish (`Renderer::m_joy`,
  fading over eight seconds) and on the podium. Uniforms: `animTime`, `crowdCars[8]` (x, z, speed), `crowdJoy`, `wind`.
- **Night**: camera flashes from the stands (`Renderer::crowdLife`: a few sprites a second per stand at random fans'
  heads, sixteen a second while a fast car passes).
- **The finish**: when the winner or the player crosses the line, confetti cannons on both sides of it and eight
  fireworks from behind them (`Renderer::celebrate`; confetti lies where it lands).
- **Banners** (`part banner`): `Renderer::buildBanners` prints the sponsor's name and Graves & Sons' slogan
  (`setSlogan`) into a texture of two rows; they are drawn unlit, fading into the fog, the text turned so it reads from
  the camera's side.
- **Wind**: `model <name> sway=0..1` bends the whole model, the top most (how pliant: reeds 1, cypresses 0.35, dead
  trees 0.15), and parts with `group=sway` (`sway=` on the part) alone; `group=flag` cloth flutters about its pole
  (`pole=x,z`, the model's middle by default; `flutter=0..1`). The strength by the venue's weather: fog 0.12, clear
  0.3, snow 0.5, rain and dust 0.65 (+0.15 on the pass), the estate's storm 1; rain that comes during a race adds to
  it. It blows the way the rain falls. There are no indoor scenes or tunnels to calm it.
- **The podium** (`Renderer::podium`, the results screen below the table): the first three cars on Graves & Sons'
  coffins stacked three, two and one high (the top ones gilded, silvered, bronzed), a derby's runners-up as smoking
  wrecks, the `podium_crowd` cheering behind, Mortimer Graves (`fig_graves`) beside the winner, Ricky Rattle
  (`fig_ricky`) at the microphone, confetti and fireworks; its own renderer (`App::m_podium`, 1280 x 256). Not after
  the test drive or a time trial.

The grandstand (`data/models/props_common.txt`): seven steps with benches and a crowd row each (about 22,400
triangles, 280 people), a canopy over the two back rows carrying the banner, four flags. Its old crowd was 448 boxes.

## Turning props and the ground

A model's parts with `group=spin` turn while the race runs (Ferris wheel, carousel, radar dish): the first of them
names `spin=<degrees a second> spin_axis=x|y|z spin_at=x,y,z`. The land beside the track gets a detail layer by
venue in the surface shader (`landStyle`): tyre tracks and specks everywhere; oil pools with a sheen (junkyard),
cracked hardpan (desert), gravel paths (cemetery, estate, whose lawn is also mown in stripes), concrete slabs with
stains and yellow lines (docks, refinery), drifts (pass), black water (swamp), pavement tiles and manholes (downtown),
sawdust paths and confetti (fairground), craters (proving ground).

## The miniature look, the sky and contact shadows (`src/RenderLook.cpp`)

**Grade.** A venue's `grade` line colours the finished picture after the tone curve: `shadows` and `highlights` tint
the darks and the lights (the colour is a tint: its brightness is taken out, only its hue and strength count;
`amount` how much of them), then `sat` (saturation), `contrast` (round a dark grey, 0.3, so that night venues keep
their darks: keep it near 1.03 there) and `vignette` (the corners' darkening). Without the line the old film's
grade: darks a little cooler, lights a little warmer.

**Miniature look** (`Settings::m_miniature`, the option "Miniatur-Look": off, on, automatic, the default; see "The
benchmark and the automatic miniature look"): a tilt-shift lens. The post pass shrinks the picture to a half and a
quarter (16-bit, before the tone curve) and blends towards them above and below a sharp band 0.4 of the picture high,
which follows the player's car (the camera looks ahead of it); a stand in the picture widens the band to take it in
(at most 0.9 of the picture, gliding; `Renderer::m_tiltHalf`), so its people are seen; with the blur on, saturation
x1.06 and contrast x1.02. Quality 0 (the Deck) has only the half-size step and 0.8 of the blur.
The garage's showroom never blurs.

**Things flying over the track** (`Renderer::sky`): after the first 10 s of a race, every 35–75 s, the Graves & Sons
airship (daylight venues with the sun above ~20°) or the Sensenblatt's news helicopter (everywhere). They fly above
the camera and are never drawn in the picture, only into the shadow map: a directional light casts the same shadow
wherever along the sun's ray a thing hangs, so they are drawn low on that ray (30–34 m above the shadow, inside the
light's depth range). The airship (48 m) drifts east or west at 7.5 m/s across the track 160 m ahead of the car the
camera follows, towing a banner whose letters ("GRAVES & SONS", the interface font's pixels as flat quads, laid
west to east so the shadow reads) lie flat; the helicopter films that car for 11 s: in from one side of the picture,
near that edge for most of the time (0.85 of the way from the middle, cut by the frame a little; it keeps to the
view, as the camera leads the car off the middle), out the same side, the four blades turning. Unlike the airship it is drawn in the picture
too (`drawFlyer( false )`, lit like the cars, its red light blinking at night): hanging 24 m straight above that point
(not on the sun's ray: with a low moon it would leave the picture; its shadow falls where the light puts it), it is
seen at the picture's edge. At night its searchlight (a spot from the helicopter itself, 14° wide) is on the car, and the
beam shows in the air as a cone of additive light (`drawBeam`: faint on a clear night, bright in rain, storm and
fog). `Renderer::m_hum` gives the race the loudness of `LoopE::ROTOR` and `LoopE::AIRSHIP`
(by the shadow's distance from the view, Doppler while it comes nearer).

**Lightning** (weather `storm`): every 6–16 s a flash of a stroke and two return strokes (`flashCurve`, 1.2 s). The
flash is a cold light from the sky that throws the moon's shadows (it goes through the shadow map) and lights the
haze and the background. Two flashes in five strike in view: a jagged channel of additive quads from the ground up
to about half the camera's height, a light, sparks, smoke and a scorch; `SfxE::THUNDER_NEAR` follows after the
sound's time from the strike, else `SfxE::THUNDER_FAR` after 1.2–4 s (`Renderer::Cue` kinds 2 and 3). The estate's
ambience keeps its own distant thunder.

**Contact shadows** (`Renderer::bakeContact`, once per track): the walls, the prop chunks, the obstacles and the
people by the road are drawn from straight above into a map over the track and its props (0.22 m a texel, 0.3 on
the Deck; at most 2048 / 3072 / 4096 texels). Seen from above a wall's sides have no area, so the vertex shader
pushes the vertices near the ground out along their faces (a skirt of two texels, at least 0.3 m): r marks such
feet of things at least ankle-high and the whole top of low things (up to 3–5 m: gravestones, walls, parked cars),
g the tops of tall things (2.5–24 m: buildings, tree crowns). A separable bell-shaped blur spreads r over about
0.45 m and g over 1.3 m, then r is doubled and g raised by 1.4 (a lone skirt is thin; blurred, its edge would be
pale). (A weighted maximum instead of the blur kept the skirts' full strength but turned the rasterised stair steps
of a diagonal wall into a row of beads.) The lighting reads it (`contactAt`): the sky's light is darkened by
0.55 x r + 0.3 x g (0.62 / 0.34 at night, where the sky's light is most of what there is), the sun's (0.7 of that)
only down to what the shadow map has not taken already (`min( shadow, ... )`, so a place in shadow does not get
darker twice); models only near the ground (fading out by 2.5 m) and their upward faces hardly. Loose things,
barrels and pickups move and are left out. Cost: one texture read per pixel and no draw call while racing; the bake
runs once when the track is set up.

## Scenes (`data/scenes/<venue>.txt`)

Composed set pieces by the road, so a venue reads as a place and not as scattered props: every 70–140 m on
alternating sides a scene of the venue (weighted, not one of the last three), placed whole where it fits, turned to
face the track; `<venue>_start` stands beside the start line. Random filler props come after, mostly within 25 m of
the road.

```
scene cemetery_funeral
  venue cemetery
  size 12                # its radius on the ground
  near 4                 # metres between the road's edge and its front
  put open_grave x=0 z=3                 # a model; x along the road, z away from it
  put crowd_mourners x=0 z=6.5 rot=180   # rot 0: its nose (-z) to the road
  car pallbearer x=-8 z=1 rot=90         # a real car in some paint ("any": one of the venue's league or below)
  wreck any x=4 z=2 rot=60 tilt=80       # rusted, sunk on its rims, tilt= degrees on its side
```

People (`data/models/props_people.txt`, written by `tools/props_people.py`): marshal, mechanic, worker, undertaker,
tramp, widow, crowds of spectators and mourners, a fire barrel; scenes are written by `tools/make_scenes.py` (the
junkyard by hand).

## Prop catalogue

The props every venue needs (the prop designer builds them; track designers may use them by these names before
they exist — `--validate` warns until they do). Lamps carry `lamp=1`.

| venue | props |
|---|---|
| all | `barrel_oil barrel_explosive tire_stack crate crate_stack cone lamp_street sign_arrow_left sign_arrow_right billboard wreck_car pillar_concrete start_gantry grandstand flag_pole` |
| pickups | `pickup_cash pickup_wrench pickup_ammo pickup_nitro pickup_mine pickup_rocket pickup_mystery` (a small coffin), `mine_live` |
| junkyard | `scrap_pile car_crusher crane_magnet wreck_stack shack_tin tire_pile oil_tank_small` |
| cemetery | `gravestone gravestone_cross gravestone_angel crypt mausoleum cypress tree_dead lamp_old chapel bench wreath open_grave` |
| docks | `container container_stack crane_harbor bollard warehouse lamp_harbor forklift ship_hull` |
| desert | `cactus cactus_tall rock_red skull_cow motel motel_sign gas_pump tumbleweed water_tower wreck_rust` |
| refinery | `tank_oil pipe_rack flare_stack barrel_stack valve catwalk lamp_sodium` |
| swamp | `tree_swamp stilt_hut boat_wreck reeds gator dock_wood lantern` |
| downtown | `building_a building_b building_tall traffic_light neon_sign hydrant bus_stop parked_car newsstand lamp_city` |
| pass | `fir_tree fir_snow rock_snow avalanche_fence cabin_wood snow_bank lift_pylon sign_curve` |
| fairground | `ferris_wheel carousel coaster_segment clown_head ticket_booth tent_striped bulb_arch balloon_stand ghost_train` |
| proving | `bunker tank_wreck tank_trap sandbag_wall watchtower crater radar_dish target_board barbed_post` |
| estate | `mausoleum_large gargoyle manor_wall chapel_gothic hedge_block tree_dead_big iron_gate fountain_skull lamp_gothic` |

## Bosses

Each league champion drives a car of their own in the showdown (`boss <car>` in data/rivals.txt; class 6, never
for sale; quick races offer the class-6 cars once a career has been won): Barry's Scrap Press, Jack Knife's
Jackknife, Petra Leum's Hellfire tanker, Gloria Mundy's Sic Transit limousine, Major Casualty's Triage half-track.

## Arenas (derby)

An arena is a track with `arena 1`: a short loop or a wide shape; the cars start spread out, drive freely and the
last car driving wins. Each venue from league 4 on has one arena.

## Rivals (`data/rivals.txt`)

```
rival rita
  name "Rigor Mortis Rita"
  league 5                   # the league they start in
  car rustbucket             # their car in that league (they move to better cars when promoted)
  colors #a22 #111 #ddd      # paint, paint2, paint3
  skill 0.45                 # 0..1 driving
  aggression 0.7             # 0..1 ramming and blocking
  weapons 0.5                # 0..1 how much they shoot and drop things
  likes mine                 # the gear they prefer
  champion 1                 # the champion of their league (one per league)
  rookie 1                   # a newcomer: enters when a rival has died (not at the start)
  portrait "..."             # how they look (English, for the portrait painter): age, face, clothes, mood
  bio "..." bio_en "..."
  say start "..." "..."      # lines they shout in the race (German), several per event
  say_en start "..." "..."
  say overtake ...           # events: start overtake wreck_player wrecked_by_player hit win lose dead
  obit "..." obit_en "..."   # their obituary in the tabloid when they die
```

There are 25 rivals at the start (five per league, one of them the champion), 20 rookies, and the boss
`mortimer_graves` (league 0). Rivals who are wrecked may die (the difficulty decides how often); their seat goes to
a rookie.

## Career (`data/career.txt`, `data/leagues.txt`)

```
league 5
  title "Kreisklasse" title_en "Gutter League"
  desc ...
  venues junkyard cemetery
  purse 1200                 # the prize of the main event's winner
  entry 0                    # entry fee of the main event
  points 10 6 4 3 2 1        # league points for places 1..6
  showdown 40                # the player may challenge the champion with this many points among the top two
  intro story_l5             # data/story: shown when the league starts
```

A **season** is 8 race days; then the league's table starts afresh. The showdown with the champion opens to a player
among the top two with the league's `showdown` points (it pays four times the purse: enough for the next class of
car). A race day offers three events from the league's venues: **small** (weaker rivals, 50 % purse, no fee), **main**,
**big** (the strongest rivals, 220 % purse, a fee). Prize per place: 100 / 60 / 40 / 25 / 15 / 10 % of the purse.
A wreck caused pays a **bounty** (10 % of the purse, more with perks), the fastest lap 10 %. Rivals not in the
player's race get simulated results, so the table moves every day.

Between races: **Garage** (buy and sell cars, upgrades, repairs, paint), **Black market** (gear, sabotage a rival
of the next race), **Loan shark** (credit at a cruel rate, collectors when it is overdue), **Sponsor** (one
contract per league: a bonus per condition), **Tabloid** (the news of the day), **Standings**.

## Difficulty (`data/difficulty.txt`)

Five difficulties with black-humour names (German and English). Each sets: `ai_speed` (share of the computer's
ideal pace), `ai_skill`, `ai_aggression`, `ai_weapons`, `damage_taken`, `damage_dealt`, `prize`, `prices`,
`repair`, `rubber` (how much the field waits for and chases the player), `deaths` (how often a wrecked rival dies),
`growth` (how fast rivals upgrade), `interest` (the loan shark's rate).

The `ai_*` factors and `rubber` act on the rivals only; `damage_taken`, `damage_dealt` and the economy act on the
player's car (`CarSetup::m_player`), also when a computer drives it in `--careersim`. Measured with
`tools/careersweep.py` (5-6 seeds, 30.10.2026, with used cars, the promotion bonus and the bosses): Schonfrist
~30 race days, Blechschaden 34-50, Totalschaden 4 of 5 careers won (56-65), Sargnagel 3 of 5 (50-65), Totentanz 2 of
6 (60-67), the others bankrupt. A beginner (`--novice 0.8`) wins Schonfrist and Blechschaden and goes bankrupt on
Totalschaden.

## Graves, feuds, used cars

Whether a wrecked rival dies is decided at the wreck (`Career::dies`, the same answer during and after the race):
the band plays a few bars of Chopin's funeral march (1837, free) over the lowered music and the rival's last
words appear. The grave keeps the cause (gun, explosion, crash, wall, spikes, other), the killer, the track and the
words; the hub's "Ehrenfriedhof" shows the stones. A rival the player wrecks twice without killing starts a feud
(news "feud"): more temper and lead, and the player is the one they ram and hunt; seasons cool feuds by one step.
Dolores sells a used car on two days of three (about half price, a third broken, from a dead rival's estate when
there is one); every car is worth at least its scrap bonus (500 + 500 per class) when traded in; Graves pays the
new league's purse on promotion.

## News, story, sponsors (`data/news/*.txt`, `data/story/*.txt`, `data/sponsors.txt`)

```
news player_win w1
  head "..." head_en "..."
  text "..." text_en "..."
ad a1
  text "..." text_en "..."          # a classified advertisement (filler, black humour)
story intro
  title "..." title_en "..."
> German prose, one paragraph per line.
> ...
story_en intro
> English prose.
sponsor eternal_rest
  title "..." title_en "..."
  desc "..." desc_en "..."
  bonus wreck=300 win=1000 clean=500   # conditions: win podium wreck clean (no damage taken) lap nogun (no shot fired)
```

News events: `player_win player_podium player_lose player_wrecked player_spree` (3+ wrecks in a race)
`player_clean rival_dead rookie league_up showdown_won showdown_lost boss_won loan_overdue loan_paid
sabotage_caught sabotage_done record bought_car day` (filler). Placeholders in texts: `{player} {rival} {track}
{venue} {car} {place} {money} {league} {days}`.

## Sound (Audio.h)

The engine synthesises every sound at start (no sample files): effects (`SfxE`), loops (`LoopE`: tyre squeal,
scraping, gravel, flamer, fire, rain, wind, the helicopter's rotor and the airship's engines overhead, and one
ambience per venue) and engine voices (up to 8 cars at once, `EngineE` kinds as in the car list, pitch and timbre
following rpm and load). Music is streamed from
`assets/music/<name>.ogg`. `GraveSfx <dir>` writes every effect, loop and engine sample as WAV.

## Voice and pictures

Ricky Rattle speaks his own name-free lines (`character announcer_voice` in data/characters.txt) at the player's
moments; `tools/make_announcer.py` designs his voice once per language with Qwen3-TTS (G:\Tools\VoiceLab) and
clones every line into assets/voice/<lang>/ricky_<event>_<n>.ogg. The tabloid's front page shows a press photo
by the news event (assets/news/<kind>_<n>.jpg, `tools/make_news_photos.py`, FLUX.2 locally in WSL). The final lap
switches to the venue's hot take (`tools/music_hot.json`). All three run on the GPU: start them through
`tools/gpu_queue.py`.

## Programs and tests

- `GravePrix` — the game. `--mute` (no audio device: every automated run uses it), `--data DIR`.
- `--validate` — reads and checks all data; exits 1 on errors.
- `--trackshot NAME FILE.png`, `--trackshots DIR` — maps (CPU only, no window).
- `--gallery FILE.png [prefix]` — the model sheet, drawn on the CPU (no window, no GPU; `--yaw DEG --cell N --cols N`).
- `--shot TRACK FILE.png [--at METRES] [--car NAME]` — a picture of the race at a place; `--sky 0|1|2` puts the
  airship, the helicopter or a lightning strike into it, `--miniature 0` turns the tilt-shift blur off, `--tow SECONDS`
  wrecks the car nearest to the view and shows Graves & Sons' hook that far in.
- `--sim TRACK [--laps n] [--seed n]`, `--simall` — computer races without a window (`--peaceful`, `--car NAME`,
  `--assist [LEVEL]`, `--hands 0..1`, `--novice 0..1`, `--flatout`: every driver alike); the lines count stuck cars and
  wall hits, `--simall` ends with the totals.
- `--handling [--car NAME] [--level N] [--wall]` — a car alone on open ground: reversing, turning circles, slides.
- `--handling --tracks [--car NAME] [--level N]` — every race track's slowest bend for that car and the bends' radii.
- `--careersim [--days N] [--seed N] [--skill 0..1] [--hands 0..1] [--novice 0..1] [--assist 0|1|2] [--jobs]` — a career
  played by the computer (the economy's test); a race lost also tells how far behind the winner. `--final CAR`
  starts at the final against Graves in that car with every upgrade and races it until it is won (the final's balance).
- `--deckcheck [TRACK] [--frames N] [--miniature 0]` — frame times at the Steam Deck's 1280 x 800 and quality 0 and 1
  (hidden window).
- `--aim [--car NAME] [--level N]` — the stick that points the way, tried on open ground (see "Seeing the road").
- `--resets` — a car put back on the track every 6 s on every track, both ways; exits 1 when one lands badly.
- `tools/careersweep.py [--difficulties ..] [--seeds ..] [--skills ..] [--novice 0..1] [--hands 0..1] [--assist 0|1|2]
  [--days N] [--exe COPY] [--tag NAME] [--workers N]` —
  `--careersim` for every difficulty and seed, three at a time at low priority, one line per career with the average
  place per league and the number of finals raced against Graves. `--exe` runs a copy of the game (with a copy of
  `data` beside it, the measurement stays fixed while the work goes on).
- `--apptest SCRIPT` — the screens played by a script (tools/apptest_*.txt; a hidden window; set GRAVE_HOME).
- `--shot` refuses while G:/Tools/Games/GPU_BELEGT.txt exists (another session's GPU job).
- `.\build.ps1` builds into bin\msvc; `.\build.ps1 -Dir <name>` into build\<name> (for a second worker).
- `Deploy/build_release.ps1` packs the Windows zip and (WSL) the Linux archive; `Deploy/build_macos.sh` the macOS zip.
- GitHub: `.github/workflows/build.yml` builds Windows and Linux on every push with the headless tests
  (`data|races|career`); `release.yml` on a tag `v*` builds, tests and packs all three (macOS compiled and checked,
  never played) and attaches the archives to the tag's draft release (made from `CHANGELOG.md` when there is none).
  Publishing stays a step by hand. The app tests need a window with OpenGL 3.3 and stay local.

## Rules for helpers working in parallel

- Work only in the files you were given. Do not commit; the lead commits.
- Build only in your own folder (`.\build.ps1 -Dir <yourname>`), never into bin\msvc.
- Every program run that could make a sound gets `--mute`. Long runs at below-normal priority.
- No GPU compute jobs (FLUX, ACE-Step, TTS): other sessions use the GPU.
