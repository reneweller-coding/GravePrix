# GRAVE PRIX – design and data contract

This file is the contract between the engine and the content. Every data file, every agent and every piece of code
follows it. When the engine changes a format, it changes this file first.

## The game

GRAVE PRIX is a top-down combat racer for PC and Steam Deck (C++20, raylib 5.5, real-time 3D seen from above,
fixed north-up camera that zooms out with speed). Six cars race on closed circuits with walls; every car has a front
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

## Driving assist

`CarSetup::m_assist` (the player's car, `Settings::m_assist`, on by default): while the car slides less than 2.5 m/s
sideways its tyres hold 30 % more (5 % more once it slides), and it turns at most a little faster than the sliding
grip carries (1.15 x grip / speed + 0.08 rad/s, against 1.2 x + 0.25 without). Measured over all tracks: the sideways slide drops
from 1.02 to 0.29 m/s, the race pace stays the same, and the career sweep stays where it was (Totalschaden 5/5,
Sargnagel 3/5, Totentanz 2/5). Without it the early cars (grip 0.88-0.95) slid like on ice and the tyre upgrades
made the game easier as it went on. `--careersim --assist 0`, `--sim --assist`.

## Balance: a person's hands, steeper leagues, upgrades with a price

`CarSetup::m_hands` (0..1) makes a computer driver drive like a person at a keyboard (`World::hands`): its inputs come
late (0.07 s practised .. 0.2 s a beginner), the steering keys know only full lock or nothing (tapped out in tenths
of a second, a little too much), the pedals only all or nothing. `--careersim --hands 0.3` (a practised player),
`--sim --hands 1`, `tools/careersweep.py --hands`. With such hands the slide was 2.7 m/s without the assist and 0.5
with it; the clean bot (no hands) hardly needs it.

The leagues get steeper (`data/leagues.txt` `pace` and `skill`: the rivals' engines and driving): league 5 is gentle
(0.86 / 0.78), league 1 harsher (0.96 / 0.95), Graves at the final 0.9 / 0.9; Sargnagel and Totentanz drive 1.5 %
faster to make up for it. Measured with the practised hands (0.3, three seeds): Schonfrist won in 77 to 143 days,
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

## Turning props and the ground

A model's parts with `group=spin` turn while the race runs (Ferris wheel, carousel, radar dish): the first of them
names `spin=<degrees a second> spin_axis=x|y|z spin_at=x,y,z`. The land beside the track gets a detail layer by
venue in the surface shader (`landStyle`): tyre tracks and specks everywhere; oil pools with a sheen (junkyard),
cracked hardpan (desert), gravel paths (cemetery, estate, whose lawn is also mown in stripes), concrete slabs with
stains and yellow lines (docks, refinery), drifts (pass), black water (swamp), pavement tiles and manholes (downtown),
sawdust paths and confetti (fairground), craters (proving ground).

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
scraping, gravel, flamer, fire, rain, wind and one ambience per venue) and engine voices (up to 8 cars at once,
`EngineE` kinds as in the car list, pitch and timbre following rpm and load). Music is streamed from
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
- `--shot TRACK FILE.png [--at METRES] [--car NAME]` — a picture of the race at a place.
- `--sim TRACK [--laps n] [--seed n]`, `--simall` — computer races without a window.
- `--careersim [--days N] [--seed N] [--skill 0..1]` — a career played by the computer (the economy's test).
- `--deckcheck [TRACK] [--frames N]` — frame times at the Steam Deck's 1280 x 800 and quality 0 and 1 (hidden window).
- `tools/careersweep.py [--difficulties ..] [--seeds ..] [--skills ..] [--novice 0..1]` — `--careersim` for every difficulty and seed,
  three at a time at low priority, one line per career.
- `--apptest SCRIPT` — the screens played by a script (tools/apptest_*.txt; a hidden window; set GRAVE_HOME).
- `--shot` refuses while G:/Tools/Games/GPU_BELEGT.txt exists (another session's GPU job).
- `.\build.ps1` builds into bin\msvc; `.\build.ps1 -Dir <name>` into build\<name> (for a second worker).

## Rules for helpers working in parallel

- Work only in the files you were given. Do not commit; the lead commits.
- Build only in your own folder (`.\build.ps1 -Dir <yourname>`), never into bin\msvc.
- Every program run that could make a sound gets `--mute`. Long runs at below-normal priority.
- No GPU compute jobs (FLUX, ACE-Step, TTS): other sessions use the GPU.
