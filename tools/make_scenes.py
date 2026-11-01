"""Writes data/scenes/<venue>.txt for the venues other than the junkyard (which is written by hand): composed set
pieces along the road, facing it. Frame: x along the road, z away from it; rot 0 turns a thing's nose to the road.

    python tools/make_scenes.py
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# venue: {scene: (size, near, [pieces])}; a piece is "put MODEL x z [rot]" / "car any|CAR x z rot" /
# "wreck any|CAR x z rot [tilt]"
S = {
    "cemetery": {
        "spectators": (9, 2, ["put crowd_mourners -3 1", "put crowd_small 3 0.5", "put gravestone -6 0", "put gravestone_cross 6 0", "put lamp_old 0 3"]),
        "start": (14, 3, ["put crowd_block -9 1", "put crowd_block 9 1", "put crowd_mourners 0 4", "put fig_marshal -3 -2.5 20",
                          "put flag_pole 13 -1", "put lamp_old -13 0", "put gravestone_angel 4 7"]),
        "funeral": (12, 4, ["put open_grave 0 3", "put crowd_mourners 0 6.5 180", "car pallbearer -8 1 90", "put fig_undertaker -4 0.5 30",
                            "put wreath 2.5 2", "put wreath -2 2.2", "put gravestone 4 7", "put gravestone_cross -4 8"]),
        "crypt": (11, 4, ["put crypt 0 4", "put gravestone_angel -6 2", "put cypress 6 5", "put cypress -7 7", "put lamp_old 4 0",
                          "put gravestone_candle -2 0.5"]),
        "rows": (11, 3, ["put gravestone -6 1", "put gravestone_cross -3 1", "put gravestone_candle 0 1", "put gravestone 3 1",
                         "put gravestone_cross 6 1", "put gravestone_cross -6 4.5", "put gravestone -3 4.5", "put gravestone_angel 0 4.5",
                         "put gravestone_candle 3 4.5", "put gravestone 6 4.5", "put fig_widow 1.5 2.7 160"]),
        "chapel": (12, 4, ["put chapel 0 6", "put bench -5 1", "put fig_widow -5 0.2 10", "put cypress 7 2", "put cypress -8 5",
                           "put lamp_old 3 0"]),
        "digger": (10, 3, ["put open_grave -3 2", "put fig_worker -0.5 1.2 -60", "put grave_plot 3 3", "put grave_plot 6 3",
                           "put obelisk -7 4", "put tree_dead 5 7"]),
        "wreck": (10, 3, ["wreck any 0 2 60", "put gravestone_cross 3 0.5", "put gravestone 4 3", "put fig_undertaker -3 0 40",
                          "put wreath -1 -0.5", "put tree_dead -6 5"]),
    },
    "docks": {
        "spectators": (9, 2, ["put crowd_row 0 1", "put crowd_small -6 1.5", "put bollard -4 -1", "put bollard 4 -1", "put container 0 5 90"]),
        "start": (14, 3, ["put container_stack -9 4 90", "put crowd_block -9 0.5", "put crowd_block 9 0.5", "put crowd_row 0 2",
                          "put fig_marshal 3 -2 -20", "put flag_pole 13 -1", "put lamp_harbor -13 0"]),
        "cranes": (13, 4, ["put crane_harbor 0 7 90", "put container_stack -7 2 90", "put container_stack 7 2 90", "put forklift 2 -1 70",
                           "put fig_worker -2 -1.5 10", "put container 0 2 90"]),
        "warehouse": (13, 4, ["put warehouse 0 7", "put pallet_stack -7 1", "put pallet_stack -5 2.5", "put forklift 4 0 100",
                              "put crate_stack 7 2", "put fig_worker 1 -1 -20"]),
        "quay": (11, 3, ["put bollard -6 0", "put bollard 0 0", "put bollard 6 0", "put buoy 3 3", "put crowd_small -3 2",
                         "put fig_tramp 5 1.5 200", "put fire_barrel 6.3 2.4"]),
        "parking": (12, 3, ["car any -7.5 1 0", "car any -4.5 1 0", "car any -1.5 1 180", "car any 1.5 1 0", "wreck any 4.5 1 10",
                            "put lamp_harbor 7 1", "put fig_mechanic 4.5 -1.5 30"]),
        "boxes": (11, 3, ["put container -5 2 90", "put container 0 2 80", "put container_stack 5 4 90", "put crate_stack -6 -1",
                          "put barrel_oil 2 -1", "put barrel_oil 2.7 -0.6", "put crowd_small -1 5"]),
    },
    "desert": {
        "spectators": (9, 2, ["put crowd_row 0 1", "car any -7 2 90", "put crowd_small 6 1", "put cactus 9 3", "put barrel_oil -3 -0.5"]),
        "start": (14, 3, ["put grandstand 0 4", "put crowd_block -10 1", "put crowd_block 10 1", "put fig_marshal -3 -2.5 20",
                          "put flag_pole 13 -1", "put cactus_tall -14 3"]),
        "motel": (13, 4, ["put motel 0 6", "put motel_sign -8 1", "car any 5 1 90", "car any 8 2 80", "put gas_pump -3 0.5",
                          "put fig_mechanic -1 -1 20"]),
        "bones": (10, 3, ["put skull_cow -3 0", "put ribcage 1 1", "put cactus 5 2", "put cactus_tall -6 4", "put vulture_post 3 5",
                          "wreck any -2 6 70"]),
        "station": (12, 4, ["put gas_pump -2 0.5", "put gas_pump 2 0.5", "put water_tower 6 6", "car any 0 3 90", "put fig_worker -5 0 40",
                            "put tumbleweed 4 -1", "put barrel_oil -6 2"]),
        "crash": (10, 3, ["wreck any 0 2 40 80", "wreck any 4 0 120", "put rock_red -5 3", "put cactus_tall 6 5", "put fig_tramp -2 -1 160",
                          "put tumbleweed 2 4"]),
        "mesa": (12, 5, ["put mesa_rock 0 6", "put cactus -6 1", "put cactus 5 0.5", "put cactus_tall 8 3", "put tumbleweed -2 0",
                         "put skull_cow 2 1.5"]),
    },
    "refinery": {
        "spectators": (9, 2, ["put crowd_row 0 1", "put crowd_small -6 1", "put barrel_stack 6 2", "put lamp_sodium -8 0"]),
        "start": (14, 3, ["put grandstand 0 4", "put crowd_block -10 1", "put crowd_block 10 1", "put fig_marshal 3 -2.5 -20",
                          "put flag_pole -13 -1", "put lamp_sodium 13 0"]),
        "tanks": (13, 5, ["put tank_oil -5 6", "put tank_oil 6 7", "put catwalk 0 2 90", "put pipe_rack 0 -1 90", "put fig_worker 3 0 -30"]),
        "flare": (12, 5, ["put flare_stack 0 6", "put distill_column -6 4", "put barrel_stack 5 1", "put fig_worker -2 0 20",
                          "put valve 2 0.5"]),
        "spill": (10, 3, ["put oil_puddle 0 1", "put oil_puddle 3 2.5", "wreck any -4 2 60", "put barrel_oil 2 -0.5",
                          "put barrel_oil 5 0.5", "put fig_worker 6 2 -100"]),
        "valves": (11, 3, ["put valve -4 1", "put valve 0 1", "put valve 4 1", "put pipe_rack 0 4 90", "put lamp_sodium 7 0",
                           "put crowd_small -6 3"]),
        "parking": (12, 3, ["car any -6 1 0", "car any -3 1 10", "car any 0 1 -5", "wreck any 3.5 1 0", "put barrel_stack 7 2",
                            "put lamp_sodium -8.5 0.5"]),
    },
    "swamp": {
        "spectators": (9, 2, ["put crowd_small -2 1", "put crowd_small 3 1.5", "put dock_wood 0 4", "put lantern -5 0", "put reeds 6 0"]),
        "start": (14, 3, ["put dock_wood 0 3", "put crowd_block -9 1", "put crowd_block 9 1", "put lantern -4 -1", "put lantern 4 -1",
                          "put fig_marshal 0 -2.5", "put flag_pole 12 -1"]),
        "hut": (12, 4, ["put stilt_hut 0 5", "put dock_wood -6 2", "put boat_wreck 6 4", "put lantern -2 0", "put fig_tramp 2 0.5 200"]),
        "gators": (10, 3, ["put bog_pool 0 3", "put gator -1 2 40", "put gator 2 4 -60", "put gator 4 1 200", "put reeds -5 1",
                           "put reeds 6 5", "put sunken_car -3 5"]),
        "trees": (11, 4, ["put tree_swamp -5 3", "put tree_swamp 4 5", "put tree_swamp 0 8", "put stump 1 1", "put reeds -2 0",
                          "put reeds 6 1", "put lantern 3 0"]),
        "wreck": (10, 3, ["put sunken_car 0 3", "wreck any -5 1 70 15", "put reeds 4 1", "put reeds 5 4", "put fig_tramp 2 0 200",
                          "put stump -2 5"]),
    },
    "downtown": {
        "spectators": (9, 2, ["put crowd_row 0 1", "put crowd_small -6 1", "put newsstand 6 2", "put lamp_city -8 0", "put hydrant 4 -0.5"]),
        "start": (14, 3, ["put crowd_block -9 1", "put crowd_block 9 1", "put bus_stop 0 2", "put lamp_city -13 0", "put lamp_city 13 0",
                          "put newsstand -4 3", "put fig_marshal 3 -2.5 -20"]),
        "block": (14, 5, ["put building_a -6 8", "put building_b 6 8", "car any -6 1 90", "car any 1 1 90", "put hydrant 4 0",
                          "put dumpster 8 2", "put lamp_city -1 0"]),
        "tower": (13, 5, ["put building_tall 0 8", "put neon_sign -6 2", "car any 5 1 90", "put crowd_small -2 1", "put lamp_city 7 0"]),
        "busstop": (11, 3, ["put bus_stop 0 1.5", "put crowd_row 0 3", "put newsstand -6 1", "put lamp_city 5 0", "put parked_van 8 3 90"]),
        "alley": (10, 3, ["put dumpster -3 2", "put dumpster 0 2.5", "put tramps_fire 4 1", "wreck any -6 4 80", "put hydrant 6 -0.5"]),
        "parking": (12, 3, ["car any -7.5 1 0", "car any -4.5 1 180", "car any -1.5 1 0", "car any 1.5 1 0", "car any 4.5 1 180",
                            "put lamp_city 7.5 0", "put parked_van 0 6 90"]),
    },
    "pass": {
        "spectators": (9, 2, ["put crowd_row 0 1", "put crowd_small 6 2", "put fir_snow -7 3", "put snow_bank 0 -0.5"]),
        "start": (14, 3, ["put cabin_wood 0 5", "put crowd_block -9 1", "put crowd_block 9 1", "put fig_marshal -3 -2.5 20",
                          "put flag_pole 13 -1", "put fir_snow -14 3"]),
        "cabin": (12, 4, ["put cabin_wood 0 5", "put snowman -5 1", "put fir_snow 6 3", "put fir_snow -8 6", "car any 4 0.5 90",
                          "put snow_bank -2 -0.5"]),
        "lift": (11, 4, ["put lift_pylon 0 4", "put fir_snow -5 2", "put fir_snow 5 6", "put fir_snow -2 8", "put crowd_small 3 1"]),
        "plow": (11, 3, ["put snow_plow 0 2 90", "put snow_bank -5 1", "put snow_bank 5 1", "put fig_worker -2 -0.5 30",
                         "put sign_curve 6 -0.5"]),
        "rocks": (10, 3, ["put rock_snow -3 2", "put rock_snow 2 3", "put rock_snow 5 1", "put avalanche_fence 0 6 90",
                          "put fir_tree -6 5"]),
        "crash": (10, 3, ["wreck any 0 2 30 70", "put snow_bank -3 0", "put fir_snow 5 4", "put fig_tramp -4 1 120"]),
    },
    "fairground": {
        "spectators": (9, 2, ["put crowd_block 0 1.5", "put crowd_small -7 1", "put balloon_stand 7 1", "put lamp_street -4 -0.5"]),
        "start": (14, 3, ["put ticket_booth 0 3", "put crowd_block -9 1", "put crowd_block 9 1", "put bulb_arch 0 6",
                          "put fig_marshal -3 -2.5 20", "put flag_pole 13 -1"]),
        "wheel": (14, 6, ["put ferris_wheel 0 8", "put crowd_row 0 2", "put balloon_stand -7 1", "put ticket_booth 7 2"]),
        "carousel": (12, 4, ["put carousel 0 6", "put crowd_small -5 1", "put clown_head 6 2", "put balloon_stand 4 0"]),
        "stalls": (12, 3, ["put tent_striped -4 4", "put high_striker 4 2", "put balloon_stand 7 0", "put crowd_small 0 0.5",
                           "put crowd_small -7 1"]),
        "ghost": (13, 5, ["put ghost_train 0 6", "put crowd_row 0 1.5", "put bulb_arch -6 2"]),
        "bumper": (11, 3, ["put bumper_car -3 2 30", "put bumper_car 1 3 -60", "put bumper_car 4 1.5 140", "put bumper_car -1 5 200",
                           "put crowd_small -6 0.5", "put lamp_street 6 0"]),
    },
    "proving": {
        "spectators": (9, 2, ["put sandbag_wall 0 -0.5 90", "put crowd_row 0 1.5", "put watchtower 8 4", "put fig_marshal -6 0"]),
        "start": (14, 3, ["put bunker 0 5", "put crowd_block -9 1", "put crowd_block 9 1", "put watchtower -13 3",
                          "put fig_marshal 3 -2.5 -20", "put flag_pole 12 -1"]),
        "targets": (11, 3, ["put target_board -5 2", "put target_board 0 3", "put target_board 5 2", "put crater -2 6",
                            "put crater 4 7", "put tank_wreck 0 9 90"]),
        "traps": (10, 3, ["put tank_trap -6 1", "put tank_trap -2 1.5", "put tank_trap 2 1", "put tank_trap 6 1.5",
                          "put sandbag_wall 0 5 90", "put fig_marshal 3 3.5 180"]),
        "radar": (12, 4, ["put radar_dish 0 6", "put bunker -6 3", "put jeep_wreck 5 1", "put fig_worker 2 0 -20", "put barbed_post -8 0"]),
        "dummies": (10, 3, ["put crash_dummy -3 1", "put crash_dummy 0 2 40", "put crash_dummy 3 0.5 -30", "wreck any 1 5 70",
                            "put barbed_post 6 0", "put barbed_post -6 0"]),
    },
    "estate": {
        "spectators": (9, 2, ["put crowd_mourners 0 1.5", "put crowd_small -6 1", "put lamp_gothic 5 0", "put hedge_block 8 3"]),
        "start": (14, 3, ["put crowd_block -9 1", "put crowd_block 9 1", "put statue_reaper 0 5", "put lamp_gothic -4 0",
                          "put lamp_gothic 4 0", "put fig_undertaker 0 -2"]),
        "mausoleum": (13, 5, ["put mausoleum_large 0 7", "put gargoyle -5 2", "put gargoyle 5 2", "put hedge_block -8 5",
                              "put hedge_block 8 5", "put crowd_mourners 0 2.5 180"]),
        "fountain": (11, 4, ["put fountain_skull 0 4", "put hedge_block -6 2", "put hedge_block 6 2", "put hedge_block -6 6",
                             "put hedge_block 6 6", "put fig_widow -2 1 20"]),
        "gate": (13, 3, ["put iron_gate 0 2", "put manor_wall -7 2", "put manor_wall 7 2", "put lamp_gothic -3 0",
                         "put lamp_gothic 3 0"]),
        "coffins": (11, 3, ["put coffin_stack -3 2", "put coffin_stack 1 3", "car pallbearer 6 1 90", "put fig_undertaker 3 0 -30",
                            "put fig_undertaker -1 0 20"]),
        "statues": (10, 4, ["put statue_reaper 0 3", "put gargoyle -5 1", "put tree_dead_big 5 5", "put tree_dead -6 6"]),
        "maze": (16, 5, ["put hedge_maze 0 9", "put lamp_gothic -9 1", "put lamp_gothic 9 1", "put fig_widow -3 1 30",
                         "put topiary_skull 9 6"]),
        "pool": (13, 4, ["put pool_hearse 0 6", "put chair_plastic -5 2 20", "put chair_plastic -3.5 2.4 -10",
                         "put fig_undertaker 5 2 -40", "put lamp_gothic -8 0", "put urn_stone 7 1"]),
        "greenhouse": (12, 5, ["put greenhouse 0 7", "put urn_stone -6 1", "put urn_stone 6 1", "put fig_worker -3 2 30",
                               "put gnome 3 2 -20"]),
        "gnomes": (11, 3, ["put gnome_army 0 4", "put gnome -6 1 20", "put gnome 6 1.5 -30", "put hedge_block 0 9",
                           "put lamp_gothic -8 0"]),
        "topiary": (13, 3, ["put topiary_skull -6 3", "put topiary_skull 0 3.5", "put topiary_skull 6 3", "put lamp_gothic -3 0",
                            "put lamp_gothic 3 0", "put hedge_block 0 8"]),
        "gazebo": (12, 4, ["put gazebo 0 6", "put crowd_mourners -5 2 160", "put bench 5 1.5", "put tree_dead 8 5",
                           "put urn_stone -2 1.5"]),
        "angel": (12, 3, ["put statue_angel 0 3", "put gravestone_angel -5 2", "put gravestone_angel 5 2", "put cypress -8 6",
                          "put cypress 8 6", "put wreath -1.5 1", "put grave_lantern 1.5 1"]),
        "family": (13, 4, ["put mausoleum -6 6", "put crypt 6 5", "put obelisk 0 3", "put wreath -2 1", "put wreath 2 1",
                           "put grave_lantern 0 1", "put lamp_gothic 9 1"]),
    },
}


# How often a scene comes (1 if not named): the big set pieces now and then, the spectators often.
WEIGHTS = {"wheel": 0.35, "carousel": 0.5, "ghost": 0.4, "cranes": 0.5, "warehouse": 0.5, "motel": 0.5, "mesa": 0.6,
           "chapel": 0.4, "crypt": 0.7, "tower": 0.6, "block": 0.8, "mausoleum": 0.5, "flare": 0.6, "tanks": 0.6,
           "radar": 0.5, "lift": 0.6, "hut": 0.7, "spectators": 1.6, "funeral": 1.2, "maze": 0.5, "pool": 0.6,
           "greenhouse": 0.6, "gazebo": 0.7}


def main():
    for venue, scenes in S.items():
        lines = ["# %s scenes (written by tools/make_scenes.py; docs/DESIGN.md \"Scenes\")." % venue.capitalize(),
                 "# Frame: x along the road, z away from it; rot 0 turns a thing's nose to the road.", ""]
        for name, (size, near, pieces) in scenes.items():
            lines += ["scene %s_%s" % (venue, name), "  venue " + venue, "  size %g" % size, "  near %g" % near]
            if name in WEIGHTS:
                lines.append("  weight %g" % WEIGHTS[name])
            for p in pieces:
                w = p.split()
                kind, what, x, z = w[0], w[1], w[2], w[3]
                s = "  %s %s x=%s z=%s" % (kind, what, x, z)
                if len(w) > 4:
                    s += " rot=" + w[4]
                if len(w) > 5:
                    s += " tilt=" + w[5]
                lines.append(s)
            lines.append("")
        path = os.path.join(ROOT, "data", "scenes", venue + ".txt")
        open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    print("scenes for", len(S), "venues")


if __name__ == "__main__":
    main()
