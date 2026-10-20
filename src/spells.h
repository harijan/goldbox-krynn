#ifndef COK_SPELLS_H
#define COK_SPELLS_H

/* The spells and spell effects by what they are, for the descriptor's
 * spells and effects sections (rules.h; DEVELOPERS.md's "Games"): the
 * spell and effect ids the code names, which each game gives its own
 * (cok_spell_id, cok_effect_id), and the port's handlers, which each
 * game's table maps its ids to (cast.c, effect.c). The handlers are shared
 * code; a game's id runs the one whose routine it shares with Champions,
 * or stops where the port does not carry its routine out yet. */

/* The most spells and scroll spells a spell list holds in any game
 * (magic.c's arrays; each game's spells section's list_spells and
 * list_scrolls are no more). */
enum { COK_SPELL_LIST_MOST = 0x80 };

/* The spell ids the code names, as Champions numbers them (its name at
 * DS:2077 + 41 * id): a game without such a spell gives 0, and code that
 * reaches it stops at cok_spell_id's assertion. */
typedef enum {
    COK_SPELL_BLESS,                /* 0x01 */
    COK_SPELL_CURE_LIGHT_WOUNDS,    /* 0x03: the computer's healer (432f:210b, 3afb) */
    COK_SPELL_MAGIC_MISSILE,        /* 0x0f: what Shield stops (3f44:062c) */
    COK_SPELL_HOLD_PERSON,          /* 0x17, the cleric's */
    COK_SPELL_KNOCK,                /* 0x1f: a locked door's Knock (475c:0720) */
    COK_SPELL_ANIMATE_DEAD,         /* 0x24: its targeting, a breath's (5b04:4eea) */
    COK_SPELL_REMOVE_CURSE,         /* 0x2b */
    COK_SPELL_FIREBALL,             /* 0x2f */
    COK_SPELL_HOLD_PERSON_MAGE,     /* 0x31, the magic-user's */
    COK_SPELL_LIGHTNING_BOLT,       /* 0x33 */
    COK_SPELL_CURE_SERIOUS_WOUNDS,  /* 0x3a */
    COK_SPELL_GIANT_STRENGTH,       /* 0x3b: an item's power */
    COK_SPELL_PARALYZE,             /* 0x3d: an item's power; its targeting, a breath's */
    COK_SPELL_FIREBALL_ITEM,        /* 0x40: an item's Fireball */
    COK_SPELL_CURE_CRITICAL_WOUNDS, /* 0x47 */
    COK_SPELL_ENTANGLE,             /* 0x4f */
    COK_SPELL_CHARM_MONSTERS,       /* 0x51 */
    COK_SPELL_CONFUSION,            /* 0x52 */
    COK_SPELL_DIMENSION_DOOR,       /* 0x53: aimed at an empty square too */
    COK_SPELL_FEAR,                 /* 0x54 */
    COK_SPELL_FIRE_SHIELD,          /* 0x55 */
    COK_SPELL_FUMBLE,               /* 0x56 */
    COK_SPELL_BLESS_POWER,          /* 0x69: a deity's granted Bless */
    COK_SPELL_NAMES
} cok_named_spell;

/* The effect ids the code names, as Champions numbers them (its handler
 * at DS:6b94 + 4 * id): a game without such an effect gives 0, and code
 * that reaches it stops at cok_effect_id's assertion. */
typedef enum {
    COK_EFFECT_SNAKES,            /* 0x03: Sticks to Snakes' (3f44:016a) */
    COK_EFFECT_BOLTS,             /* 0x04: a monster's energy bolts (5b04:4ab8) */
    COK_EFFECT_DETECT_MAGIC,      /* 0x05 */
    COK_EFFECT_CHARMED,           /* 0x0b (3f44:03cd) */
    COK_EFFECT_ENLARGED,          /* 0x0c: Enlarge's strength (5b04:21d7) */
    COK_EFFECT_NEW_FORM,          /* 0x0d: at death, mad or a new form (3f44:04c2) */
    COK_EFFECT_FRIENDLY,          /* 0x0e: Friends' charisma (5b04:23ac) */
    COK_EFFECT_POISON,            /* 0x0f: poison, ticking (3f44:05c3) */
    COK_EFFECT_READ_MAGIC,        /* 0x10: reads any scroll (5b04:0981) */
    COK_EFFECT_RESIST_CHARM_30,   /* 0x12 (3f44:065d) */
    COK_EFFECT_SLOW_POISON,       /* 0x16: Slow Poison's (3f44:0725) */
    COK_EFFECT_HAMMER,            /* 0x17: the spiritual hammer (3f44:07b5) */
    COK_EFFECT_SEES_INVISIBLE,    /* 0x18: what invisibility does not hide from (3f44:09b3) */
    COK_EFFECT_INVISIBLE,         /* 0x19 (3f44:09b3) */
    COK_EFFECT_TARGET_BONUS,      /* 0x1a: to hit a kind of target (3f44:09e8) */
    COK_EFFECT_MIRROR_IMAGES,     /* 0x1c (3f44:0a2f) */
    COK_EFFECT_NAUSEA,            /* 0x1e: the stinking cloud's (3f44:0ae0) */
    COK_EFFECT_HELPLESS,          /* 0x1f (3f44:00f7) */
    COK_EFFECT_MAD,               /* 0x20: at death, mad (3f44:0bc9) */
    COK_EFFECT_BLIND,             /* 0x21 (3f44:0cf0) */
    COK_EFFECT_DISEASE,           /* 0x22: Cause Disease's (3f44:0d19) */
    COK_EFFECT_CONFUSED,          /* 0x23 (3f44:0d7c) */
    COK_EFFECT_CURSED,            /* 0x24: Bestow Curse's (3f44:0f3f) */
    COK_EFFECT_STRENGTH,          /* 0x26: Strength's (5b04:2e4e) */
    COK_EFFECT_HASTED,            /* 0x27 (3f44:0f78) */
    COK_EFFECT_CLOUD,             /* 0x28: a cloud's end (3f44:0ff7) */
    COK_EFFECT_SLOWED,            /* 0x2a (3f44:144c) */
    COK_EFFECT_STRENGTH_DRAIN,    /* 0x2b (3f44:1469) */
    COK_EFFECT_WASTING,           /* 0x2c (3f44:1504) */
    COK_EFFECT_HARD_TO_HIT,       /* 0x2f: an attack roll 4 lower (3f44:15a3) */
    COK_EFFECT_IMMOLATE,          /* 0x30 (3f44:15cd) */
    COK_EFFECT_HOT_FIRE_SHIELD,   /* 0x32 (3f44:173a) */
    COK_EFFECT_HELD,              /* 0x33 (3f44:00f7) */
    COK_EFFECT_PARALYZED,         /* 0x34 (3f44:00f7) */
    COK_EFFECT_ASLEEP,            /* 0x35 (3f44:00f7) */
    COK_EFFECT_COLD_FIRE_SHIELD,  /* 0x36 (3f44:176b) */
    COK_EFFECT_POISONED,          /* 0x37 (3f44:179c) */
    COK_EFFECT_PLUS_TWO,          /* 0x39: to hit and AC 2 better (3f44:17c6) */
    COK_EFFECT_DEATH_THROES,      /* 0x3a (3f44:17ea) */
    COK_EFFECT_IMMUNE,            /* 0x3b: no damage or effect from a spell (3f44:1891) */
    COK_EFFECT_BURST,             /* 0x3c (3f44:18b9) */
    COK_EFFECT_REGENERATING,      /* 0x3e: a constitution of 20 or more (3f44:1acd) */
    COK_EFFECT_MINOR_GLOBE,       /* 0x3f (3f44:1b18) */
    COK_EFFECT_POISON_TOUCH,      /* 0x40: poison on a hit (3f44:1c21), Poison's */
    COK_EFFECT_POISON_TOUCH_4,    /* 0x41: the same, 4 better (3f44:1c34) */
    COK_EFFECT_ZAP,               /* 0x42 (3f44:1c54) */
    COK_EFFECT_EXPLODES,          /* 0x44: at death (3f44:1f97) */
    COK_EFFECT_PARALYZING,        /* 0x45: paralysis on a hit (3f44:200b) */
    COK_EFFECT_DISPELLING,        /* 0x4a: a spell miscast (3f44:26a5) */
    COK_EFFECT_GIANT_SLAYER,      /* 0x4b (3f44:26bf) */
    COK_EFFECT_GATE,              /* 0x4c (3f44:272a) */
    COK_EFFECT_BERSERK,           /* 0x4d (3f44:29d5) */
    COK_EFFECT_FIREBALLS,         /* 0x4e: a monster's fireball (5b04:4dc2) */
    COK_EFFECT_ACID_DEATH,        /* 0x46: acid at death (3f44:2037) */
    COK_EFFECT_ACID_POOL,         /* 0x50: an acid pool's (3f44:2cad) */
    COK_EFFECT_PARALYZING_HARMLESS, /* 0x51: no damage, then paralysis (3f44:3009) */
    COK_EFFECT_COLD_BREATH,       /* 0x53 (5b04:546e) */
    COK_EFFECT_POISON_BREATH,     /* 0x54 (5b04:4eea) */
    COK_EFFECT_LIGHTNING_BREATH,  /* 0x55 (5b04:50cf) */
    COK_EFFECT_POISON_TOUCH_MINUS_2, /* 0x56 (3f44:31bc) */
    COK_EFFECT_POISON_TOUCH_1,    /* 0x57 (3f44:31cf) */
    COK_EFFECT_PARALYZING_HUMANS, /* 0x58: paralysis of a race above 1 (3f44:31e2) */
    COK_EFFECT_ACID_BREATH,       /* 0x5a (5b04:5227) */
    COK_EFFECT_YELLED_AT,         /* 0x5b: a yell's (60f4:0023) */
    COK_EFFECT_FEARLESS,          /* 0x5c */
    COK_EFFECT_CONSTITUTION_SAVE, /* 0x5e (3f44:32a8) */
    COK_EFFECT_RESIST_CHARM_90,   /* 0x5f (3f44:3328) */
    COK_EFFECT_POISON_IMMUNE,     /* 0x63 (3f44:3386) */
    COK_EFFECT_FROST_BREATH,      /* 0x06, a dragon's (5b04:546e) */
    COK_EFFECT_FIRE_BREATH,       /* 0x68 (5b04:546e) */
    COK_EFFECT_RANGER,            /* 0x69: a ranger's against giants (3f44:349c) */
    COK_EFFECT_BANE,              /* 0x73: 3 more against +0x13f bit 5 (3f44:37b0) */
    COK_EFFECT_STUNNED,           /* 0x6a (3f44:34db) */
    COK_EFFECT_TERROR,            /* 0x6f (3f44:3692) */
    COK_EFFECT_FIRE_SHIELD,       /* 0x70 (3f44:36e1) */
    COK_EFFECT_GIANT_STRENGTH,    /* 0x71 */
    COK_EFFECT_DEADLY,            /* 0x72: the holder's hit points (3f44:376f) */
    COK_EFFECT_POISON_RESISTANCE, /* 0x74 */
    COK_EFFECT_SICKENED,          /* 0x76: to hit 2 worse (3f44:386a) */
    COK_EFFECT_AFRAID,            /* 0x77: to hit 1 worse (3f44:3876) */
    COK_EFFECT_NAMES
} cok_named_effect;

/* The port's spell handlers (cast.c), each a routine of the original's
 * handler table (Champions' DS:6e3a + 4 * id, filled by 5b04:58ed). 0: the
 * id has none. */
typedef enum {
    COK_CAST_NONE,
    COK_CAST_BLESS,
    COK_CAST_CURSE,
    COK_CAST_CURE_LIGHT,
    COK_CAST_CAUSE_LIGHT,
    COK_CAST_AFFECTED,
    COK_CAST_PROTECTED,
    COK_CAST_COLD_RESISTANT,
    COK_CAST_BURNING_HANDS,
    COK_CAST_CHARM_PERSON,
    COK_CAST_ENLARGE,
    COK_CAST_REDUCE,
    COK_CAST_FRIENDS,
    COK_CAST_MAGIC_MISSILE,
    COK_CAST_SHIELDED,
    COK_CAST_SHOCKING_GRASP,
    COK_CAST_SLEEP,
    COK_CAST_HOLD,
    COK_CAST_FIRE_RESISTANT,
    COK_CAST_SILENCE,
    COK_CAST_SLOW_POISON,
    COK_CAST_SNAKE_CHARM,
    COK_CAST_HAMMER,
    COK_CAST_INVISIBLE,
    COK_CAST_KNOCK,
    COK_CAST_MIRROR_IMAGE,
    COK_CAST_WEAKENED,
    COK_CAST_STINKING_CLOUD,
    COK_CAST_STRENGTH,
    COK_CAST_NOTHING,
    COK_CAST_CURE_BLINDNESS,
    COK_CAST_BLINDED,
    COK_CAST_CURE_DISEASE,
    COK_CAST_DISEASED,
    COK_CAST_DISPEL_MAGIC,
    COK_CAST_PRAYER,
    COK_CAST_REMOVE_CURSE,
    COK_CAST_BESTOW_CURSE,
    COK_CAST_BLINK,
    COK_CAST_FIREBALL,
    COK_CAST_HASTE,
    COK_CAST_LIGHTNING_BOLT,
    COK_CAST_SLOW,
    COK_CAST_SPEEDY,
    COK_CAST_CURE_SERIOUS,
    COK_CAST_GIANT_STRENGTH,
    COK_CAST_ITEM_BOLT,
    COK_CAST_PARALYZED,
    COK_CAST_POTION,
    COK_CAST_ITEM_DAMAGE,
    COK_CAST_CAUSE_SERIOUS,
    COK_CAST_NEUTRALIZE_POISON,
    COK_CAST_POISON,
    COK_CAST_STICKS_TO_SNAKES,
    COK_CAST_CHARM_MONSTERS,
    COK_CAST_CONFUSION,
    COK_CAST_DIMENSION_DOOR,
    COK_CAST_FEAR,
    COK_CAST_FIRE_SHIELD,
    COK_CAST_FUMBLE,
    COK_CAST_ICE_STORM,
    COK_CAST_EMPTY_TEXT,
    /* Damage of the entry's dice through the spell's apply (dkk Flame
     * Strike 5dc5:4160, Ice Storm 48ef: Champions' 5b04:1fc2 and 4914 with
     * other dice and types). */
    COK_CAST_DICE,
    /* The spell applied, saying the entry's text (dkk Barkskin 5dc5:1daa,
     * Fire Touch 4b7d: Champions' kinds of that shape, with words of their
     * own). */
    COK_CAST_SAYING,
    /* 1cbd with no bonus, saying the entry's text (dkk Faerie Fire
     * 5dc5:4391). */
    COK_CAST_MISSILES,
    /* Cure Serious Wounds' heal of 3d8 + 3, the deity's d8 rolled before the
     * targets are counted (dkk Cure Critical Wounds 5dc5:4037). */
    COK_CAST_CURE_CRITICAL,
    COK_CAST_HANDLERS
} cok_cast_handler;

/* The port's effect handlers (effect.c), each a routine of the original's
 * handler table (Champions' DS:6b94 + 4 * id, filled by 5b04:58ed and
 * 3f44:38ea). 0: the id has none (the original calls 0000:0000). */
typedef enum {
    COK_FX_MISSING,
    COK_FX_NOTHING,
    COK_FX_BLESS,
    COK_FX_CURSE,
    COK_FX_SNAKES,
    COK_FX_BREATH,
    COK_FX_KENDER,
    COK_FX_PROTECTION_EVIL,
    COK_FX_PROTECTION_GOOD,
    COK_FX_RESIST_COLD,
    COK_FX_CHARM,
    COK_FX_NEW_FORM,
    COK_FX_TICKING,
    COK_FX_SHIELD,
    COK_FX_RESIST_CHARM_30,
    COK_FX_RESIST_FIRE,
    COK_FX_SILENCED,
    COK_FX_POISONED,
    COK_FX_HAMMER,
    COK_FX_INVISIBLE,
    COK_FX_TARGET_BONUS,
    COK_FX_HELD,
    COK_FX_IMAGES,
    COK_FX_QUARTER_LESS,
    COK_FX_COUGHING,
    COK_FX_BLIND,
    COK_FX_DISEASE,
    COK_FX_CONFUSED,
    COK_FX_MINUS_FOUR,
    COK_FX_BLINK,
    COK_FX_HASTE,
    COK_FX_HASTE_AGING,
    COK_FX_CLOUD,
    COK_FX_AVOID,
    COK_FX_SLOW,
    COK_FX_DRAIN,
    COK_FX_AGAINST_FLAG4,
    COK_FX_IMMOLATE,
    COK_FX_PRAYER,
    COK_FX_COLD_RESISTANT,
    COK_FX_FIRE_RESISTANT,
    COK_FX_ADD_INVISIBLE_1,
    COK_FX_PLUS_TWO,
    COK_FX_DEATH_THROES,
    COK_FX_IMMUNE,
    COK_FX_BURST,
    COK_FX_FIRE_RESISTANCE,
    COK_FX_REGENERATE,
    COK_FX_MINOR_GLOBE,
    COK_FX_POISON,
    COK_FX_ZAP,
    COK_FX_LOSE_WEAPON,
    COK_FX_EXPLODES,
    COK_FX_PARALYSIS,
    COK_FX_REFLECT,
    COK_FX_TERRIBLE,
    COK_FX_UNAFFECTED,
    COK_FX_DISPELLING,
    COK_FX_GIANT_SLAYER,
    COK_FX_GATE,
    COK_FX_BERSERK,
    COK_FX_STENCH,
    COK_FX_FEAR,
    COK_FX_DISPLACEMENT,
    COK_FX_HALF_BLUNT,
    COK_FX_CONSTITUTION_SAVE,
    COK_FX_RESIST_CHARM_90,
    COK_FX_RESIST_CHARM,
    COK_FX_FREE_ACTION,
    COK_FX_COLD_IMMUNE,
    COK_FX_POISON_IMMUNE,
    COK_FX_HALF_EDGED,
    COK_FX_HOLY_WATER,
    COK_FX_MAGIC_WEAPONS,
    COK_FX_SLAYER,
    COK_FX_STUNNED,
    COK_FX_TURN,
    COK_FX_ADD_INVISIBLE_255,
    COK_FX_KNIGHT,
    COK_FX_TERROR,
    COK_FX_FIRE_SHIELD,
    COK_FX_DISRUPT,
    COK_FX_MINUS_TWO,
    COK_FX_MINUS_ONE,
    COK_FX_ITEM,
    /* Death Knights' own (dkk 504d). */
    COK_FX_KENDER_BATTLE,
    COK_FX_HALF_WEAPONS,
    COK_FX_CANCEL,
    COK_FX_SMOKE,
    COK_FX_SMOKED,
    COK_FX_ARMOUR,          /* 0x0d, 0x39, 0x3c: the AC by the entry's value */
    COK_FX_SLOW_MOVEMENT,   /* 0x2a: the rate and the movement halved */
    COK_FX_PLUS_WEAPONS,    /* 0x42: no damage but from items of the entry's bonus */
    COK_FX_TYPE_HALF,       /* 0x53: half damage of the entry's types */
    COK_FX_TYPE_DOUBLE,     /* 0x4b: double */
    COK_FX_TYPE_NONE,       /* 0x41: none */
    COK_FX_DRAIN_LEVELS,    /* 0x43, 0x44: the target loses the entry's levels */
    COK_FX_HEAL_ROUND,      /* 0x54: the entry's hit points healed */
    COK_FX_FIRE_HEALS,      /* 0x4c */
    COK_FX_IRON_GOLEM,      /* 0x57 */
    COK_FX_FLESH_GOLEM,     /* 0x65 */
    COK_FX_FIRE_TOUCH,      /* 0x4a */
    COK_FX_TIER,            /* 0x6e: the tier by the entry's class's level */
    COK_FX_UNSEEN,          /* 0x20 */
    COK_FX_DISRUPT_UNDEAD,  /* 0x75 */
    COK_FX_TURN_BATTLE,     /* 0x6b */
    COK_FX_NAUSEA,          /* 0x1e */
    COK_FX_POISON_COLLAPSE, /* 0x16 */
    /* A monster's attacks on event 0x0e and 0x0f, through the breathe hook
     * (cok_cast_breath) as COK_FX_BREATH: dkk 5dc5:51cc (0x5a, 0x64, 0x68,
     * the effects section's breaths), 4fd3 (0x51, a gaze casting the
     * entry's spell), 4f97 (0x4e, casting the entry's spell), 504d:186e
     * (0x46, flames around). */
    COK_FX_DRAGON_BREATH,
    COK_FX_GAZE,
    COK_FX_CAST,
    COK_FX_FLAMES,
    COK_FX_HANDLERS
} cok_effect_handler;

/* How long a spell lasts where it is not the caster level times byte 5
 * of the spell table plus byte 4 (cok_cast_duration, 5b04:0f78). */
typedef enum {
    COK_DURATION_FIXED,       /* minutes */
    COK_DURATION_D6_TENS,     /* 1d6 * 10 */
    COK_DURATION_5D4,         /* 5d4 */
    COK_DURATION_D4_TENS_40,  /* 1d4 * 10 + 40 */
    COK_DURATION_INVISIBLE,   /* 2d10 * 10 in combat, else (1d10 + 10) * 10 */
} cok_duration_rule;

#endif
