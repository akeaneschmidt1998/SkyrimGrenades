// Builds FirePots.esp (ESL-flagged) with Mutagen, then reads it back and checks every record.
//
// Four pot types: Fire, Frost, Smoke, Thunder. Each has
//   FP_<Type>Pot            SCRL  the item: a scroll, so it sits in the Scrolls tab, can be bought,
//                                 sold, stashed, favorited and equipped to a hand
//   FP_<Type>PotEffect      MGEF  the scroll's effect: does nothing itself (script archetype) and
//                                 fires a harmless invisible projectile, so casting it plays the
//                                 normal hand-release animation; FirePots.dll then launches the
//                                 real pot with the wind-up speed. Also carries the description
//   FP_<Type>PotProjectile  PROJ  what flies: a missile with gravity that explodes on any contact
//   FP_<Type>PotExplosion   EXPL  the shatter: sounds, decal, and what the pot does:
//     Fire     fire-damage burst (FP_FirePotBurstEnch) + vanilla burning patch
//     Frost    frost-damage burst (FP_FrostPotBurstEnch, also drains stamina)
//     Smoke    a lingering cloud (FP_SmokeCloud hazard) that makes anyone inside invisible,
//              which ends as soon as they attack or cast (vanilla invisibility rules)
//     Thunder  a flash (blinding image-space effect near the blast) and a bang that makes
//              everyone up to level 25 in the blast flee for 10 s (FP_ThunderPotBurstEnch)
//
// No recipes: pots are made anywhere with FirePots.dll's pot-making menu (craft key, H).
// Meshes: models are left empty; FirePots.dll copies them from vanilla forms at load.
//
// Usage: MakePlugin <output folder>

using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using Noggog;  // ExtendedList

const string PluginName = "FirePots.esp";

var skyrim = ModKey.FromFileName("Skyrim.esm");
FormKey Sk(uint id) => new FormKey(skyrim, id);

// Vanilla forms, IDs from Mutagen.Bethesda.FormKeys.SkyrimSE.
var L_PROJECTILE = Sk(0x088767);
var EitherHand = Sk(0x013F44);
var VendorItemScroll = Sk(0x0A0E57);
var VendorItemPotion = Sk(0x08CDEC);
var TRPOilLampBreak = Sk(0x08BBC6);  // shatter sound, every pot
const float BlastRadius = 160f;      // units (~2.3 m); the arc preview draws this ring

var outDir = args.Length > 0 ? args[0] : ".";
Directory.CreateDirectory(outDir);
var outPath = Path.Combine(outDir, PluginName);

var modKey = ModKey.FromFileName(PluginName);
var mod = new SkyrimMod(modKey, SkyrimRelease.SkyrimSE);
mod.ModHeader.Author = "akeaneschmidt1998";
mod.ModHeader.Description = "Fire Pots: throwable fire, frost, smoke and thunder pots with wind-up and arc preview (requires FirePots.dll / SKSE).";
mod.IsSmallMaster = true;  // ESL flag: takes no load order slot
FormKey Own(uint id) => new FormKey(modKey, id);

// --- Shared: the invisible projectile the scrolls "cast" (so the hand plays its release) -------
var nullProjectile = new Projectile(Own(0x8F0), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_NullProjectile",
    Type = Projectile.TypeEnum.Missile,
    Flags = 0,
    Gravity = 0f,
    Speed = 100f,
    Range = 10f,
    CollisionRadius = 1f,
    Lifetime = 0.05f,
    SoundLevel = (uint)SoundLevel.Silent,
};
nullProjectile.CollisionLayer.SetTo(L_PROJECTILE);
mod.Projectiles.Add(nullProjectile);

// --- Smoke: cloud hazard -> spell -> invisibility ----------------------------------------------
var smokeInvis = new MagicEffect(Own(0x857), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_SmokeInvisibility",
    Name = "Hidden in Smoke",
    Description = "Hidden in the smoke.",
    Flags = MagicEffect.Flag.NoMagnitude | MagicEffect.Flag.NoArea | MagicEffect.Flag.Recover | MagicEffect.Flag.Painless | MagicEffect.Flag.NoHitEvent,
    Archetype = new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Invisibility),
    CastType = CastType.FireAndForget,
    TargetType = TargetType.Touch,
    CastingSoundLevel = SoundLevel.Silent,
};
mod.MagicEffects.Add(smokeInvis);

var smokeSpell = new Spell(Own(0x856), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_SmokeCloudSpell",
    Name = "Smoke",
    Description = "Hidden in the smoke.",
    Type = SpellType.Spell,
    Flags = SpellDataFlag.ManualCostCalc | SpellDataFlag.NoAbsorbOrReflect,
    CastType = CastType.FireAndForget,
    TargetType = TargetType.Touch,
};
var smokeSpellEffect = new Effect { Data = new EffectData { Magnitude = 0, Area = 0, Duration = 2 } };
smokeSpellEffect.BaseEffect.SetTo(smokeInvis.FormKey);
smokeSpell.Effects.Add(smokeSpellEffect);
mod.Spells.Add(smokeSpell);

var smokeCloud = new Hazard(Own(0x855), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_SmokeCloud",
    Name = "Smoke",
    Limit = 4,
    Radius = 250f,
    Lifetime = 20f,
    TargetInterval = 1f,  // re-applies the 2 s invisibility every second while you're inside
    Flags = Hazard.Flag.DropToGround,
};
smokeCloud.Spell.SetTo(smokeSpell.FormKey);
mod.Hazards.Add(smokeCloud);

// --- Thunder: fear effect ------------------------------------------------------------------------
var thunderFear = new MagicEffect(Own(0x864), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_ThunderFear",
    Name = "Thunderstruck",
    Description = "Flees in terror.",
    Flags = MagicEffect.Flag.Hostile | MagicEffect.Flag.Detrimental | MagicEffect.Flag.NoArea,
    MagicSkill = ActorValue.Illusion,
    Archetype = new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Demoralize),
    CastType = CastType.FireAndForget,
    TargetType = TargetType.Touch,
    CastingSoundLevel = SoundLevel.Loud,
};
mod.MagicEffects.Add(thunderFear);

// --- Pot types. Fixed form IDs: FirePots.dll looks these up, so they must never move. -----------
var types = new[]
{
    new PotType("Fire", "Fire Pot", BaseId: 0x800, ItemId: 0x820, EffectId: 0x840, Value: 15,
        Description: "A clay vessel of something flammable. Shatters in a burst of flame and leaves the ground burning.",
        Keyword: Sk(0x01CEAD),                                    // MagicDamageFire
        Burst: (Sk(0x04605A), 30f, 0),                            // EnchFireDamageFFContact
        Sound2: Sk(0x042526), ImpactSet: Sk(0x01C2AF),            // oil ignite; scorch
        Placed: Sk(0x03E074),                                     // FireSmLongHazard
        Flash: null, AiSound: SoundLevel.Silent),
    new PotType("Frost", "Frost Pot", BaseId: 0x810, ItemId: 0x830, EffectId: 0x841, Value: 20,
        Description: "A clay vessel packed with frost. Shatters in a burst of cold that saps stamina.",
        Keyword: Sk(0x01CEAE),                                    // MagicDamageFrost
        Burst: (Sk(0x04605B), 30f, 0),                            // EnchFrostDamageFFContact
        Sound2: Sk(0x023992), ImpactSet: Sk(0x018A2E),            // ice explosion; frost decal
        Placed: null, Flash: null, AiSound: SoundLevel.Silent),
    new PotType("Smoke", "Smoke Pot", BaseId: 0x850, ItemId: 0x858, EffectId: 0x842, Value: 15,
        Description: "A jar of smouldering filler. Shatters into a lingering cloud; anyone inside is hidden until they strike.",
        Keyword: null, Burst: null,
        Sound2: null, ImpactSet: null,                            // just the shatter
        Placed: smokeCloud.FormKey, Flash: null, AiSound: SoundLevel.Silent),
    new PotType("Thunder", "Thunder Pot", BaseId: 0x860, ItemId: 0x868, EffectId: 0x843, Value: 25,
        Description: "A sealed jar of volatile salts. Bursts in a blinding flash and a thunderclap that sends the weak-willed fleeing.",
        Keyword: null,
        Burst: (thunderFear.FormKey, 25f, 10),                    // up to level 25, 10 s
        Sound2: Sk(0x0A0F52), ImpactSet: Sk(0x038B05),            // Unrelenting Force thunder; shock scorch
        Placed: null,
        Flash: (Sk(0x07E8FD), 700f),                              // alduinExplosionFlashImod within 700 units
        AiSound: SoundLevel.Loud),
};

foreach (var t in types)
{
    // the scroll's effect: casting it plays the release animation and launches FP_NullProjectile
    var potEffect = new MagicEffect(Own(t.EffectId), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}PotEffect",
        Name = t.Name,
        Description = t.Description,
        Flags = MagicEffect.Flag.HideInUI | MagicEffect.Flag.NoDuration | MagicEffect.Flag.NoMagnitude |
                MagicEffect.Flag.NoArea | MagicEffect.Flag.NoHitEvent | MagicEffect.Flag.Painless,
        Archetype = new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Script),
        CastType = CastType.FireAndForget,
        TargetType = TargetType.Aimed,
        CastingSoundLevel = SoundLevel.Silent,
    };
    potEffect.Projectile.SetTo(nullProjectile.FormKey);
    mod.MagicEffects.Add(potEffect);

    ObjectEffect? burst = null;
    if (t.Burst is { } b)
    {
        burst = new ObjectEffect(Own(t.BaseId + 3), SkyrimRelease.SkyrimSE)
        {
            EditorID = $"FP_{t.Tag}PotBurstEnch",
            Name = $"{t.Name} Burst",
            Flags = ObjectEffect.Flag.NoAutoCalc,
            CastType = CastType.FireAndForget,
            TargetType = TargetType.Touch,
            EnchantType = ObjectEffect.EnchantTypeEnum.Enchantment,
        };
        var effect = new Effect { Data = new EffectData { Magnitude = b.Magnitude, Area = 0, Duration = b.Duration } };
        effect.BaseEffect.SetTo(b.Effect);
        burst.Effects.Add(effect);
        mod.ObjectEffects.Add(burst);
    }

    var explosion = new Explosion(Own(t.BaseId + 2), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}PotExplosion",
        Name = t.Name,
        Force = t.Tag is "Fire" or "Frost" ? 30f : 0f,
        Damage = t.Tag is "Fire" or "Frost" ? 5f : 0f,
        Radius = BlastRadius,
        ISRadius = t.Flash?.Radius ?? 0f,
        VerticalOffsetMult = 0f,
        Flags = 0,
        SoundLevel = t.AiSound,  // how far AI "hears" it; its own sounds always play
    };
    if (burst is not null) explosion.ObjectEffect.SetTo(burst.FormKey);
    explosion.Sound1.SetTo(TRPOilLampBreak);
    if (t.Sound2 is { } s2) explosion.Sound2.SetTo(s2);
    if (t.ImpactSet is { } imp) explosion.ImpactDataSet.SetTo(imp);
    if (t.Placed is { } placed) explosion.PlacedObject.SetTo(placed);
    if (t.Flash is { } flash) explosion.ImageSpaceModifier.SetTo(flash.Imod);
    mod.Explosions.Add(explosion);

    // Missile + Gravity: ballistic arc, explodes on the first thing it touches.
    // Speed is overwritten by FirePots.dll on every throw (wind-up); Gravity 1 = normal gravity.
    var projectile = new Projectile(Own(t.BaseId + 1), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}PotProjectile",
        Name = t.Name,
        Type = Projectile.TypeEnum.Missile,
        Flags = Projectile.Flag.Explosion,
        Gravity = 1.0f,
        Speed = 1000f,
        Range = 30000f,
        CollisionRadius = 8f,
        Lifetime = 10f,
        ImpactForce = 0f,
        SoundLevel = (uint)SoundLevel.Silent,
    };
    projectile.Explosion.SetTo(explosion.FormKey);
    projectile.CollisionLayer.SetTo(L_PROJECTILE);
    mod.Projectiles.Add(projectile);

    var keywords = new ExtendedList<IFormLinkGetter<IKeywordGetter>>
    {
        VendorItemScroll.ToLink<IKeywordGetter>(),
        VendorItemPotion.ToLink<IKeywordGetter>(),
    };
    if (t.Keyword is { } kw) keywords.Add(kw.ToLink<IKeywordGetter>());
    var pot = new Scroll(Own(t.ItemId), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}Pot",
        Name = t.Name,
        Description = t.Description,
        Value = t.Value,
        Weight = 1.0f,
        Keywords = keywords,
        BaseCost = 0,
        Flags = SpellDataFlag.ManualCostCalc | SpellDataFlag.NoDualCastModification,
        Type = (SpellType)13,  // scroll
        ChargeTime = 0.3f,     // the hand comes up into its wind-up pose quickly; the throw is the release
        CastType = CastType.FireAndForget,
        TargetType = TargetType.Aimed,
    };
    pot.EquipmentType.SetTo(EitherHand);
    var potSpellEffect = new Effect { Data = new EffectData { Magnitude = 0, Area = 0, Duration = 0 } };
    potSpellEffect.BaseEffect.SetTo(potEffect.FormKey);
    pot.Effects.Add(potSpellEffect);
    mod.Scrolls.Add(pot);
}

mod.BeginWrite
    .ToPath(outPath)
    .WithLoadOrder(skyrim)
    .WithNoDataFolder()
    .Write();

// --- Verify by reading the file back -------------------------------------------------------
using var check = SkyrimMod.CreateFromBinaryOverlay(outPath, SkyrimRelease.SkyrimSE);

void Require(bool ok, string what)
{
    if (!ok)
    {
        Console.Error.WriteLine($"VERIFY FAILED: {what}");
        Environment.Exit(1);
    }
}

Require(check.IsSmallMaster, "plugin is ESL-flagged");
var masters = check.ModHeader.MasterReferences.Select(m => m.Master).ToList();
Require(masters.SequenceEqual(new[] { skyrim }), $"only master is Skyrim.esm (got {string.Join(", ", masters)})");
Require(!check.ConstructibleObjects.Any() && !check.MiscItems.Any(), "no recipes or misc items");
Require(check.Projectiles.Any(p => p.FormKey.ID == 0x8F0 && !p.Flags.HasFlag(Projectile.Flag.Explosion)), "null projectile does nothing");

foreach (var t in types)
{
    var pot = check.Scrolls.Single(m => m.FormKey.ID == t.ItemId);
    Require(pot.Name?.String == t.Name, $"{t.Name} item");
    Require((int)pot.Type == 13 && pot.EquipmentType.FormKey == EitherHand, $"{t.Name} is an either-hand scroll");
    Require(pot.Effects.Count == 1 && pot.Effects[0].BaseEffect.FormKey.ID == t.EffectId, $"{t.Name} scroll effect");
    var me = check.MagicEffects.Single(x => x.FormKey.ID == t.EffectId);
    Require(me.Archetype is IMagicEffectArchetypeGetter arch && arch.Type == MagicEffectArchetype.TypeEnum.Script, $"{t.Name} effect does nothing itself");
    Require(me.Projectile.FormKey.ID == 0x8F0, $"{t.Name} effect casts the null projectile");

    var p = check.Projectiles.Single(x => x.FormKey.ID == t.BaseId + 1);
    Require(p.Type == Projectile.TypeEnum.Missile && p.Flags.HasFlag(Projectile.Flag.Explosion) && p.Gravity > 0.5f, $"{t.Name} projectile is an exploding missile with gravity");
    Require(p.Explosion.FormKey.ID == t.BaseId + 2, $"{t.Name} projectile -> explosion");

    var e = check.Explosions.Single(x => x.FormKey.ID == t.BaseId + 2);
    Require(t.Burst is null ? e.ObjectEffect.IsNull : e.ObjectEffect.FormKey.ID == t.BaseId + 3, $"{t.Name} explosion -> burst");
    Require(t.Placed is null ? e.PlacedObject.IsNull : e.PlacedObject.FormKey == t.Placed, $"{t.Name} explosion placed object");
    Require(e.SoundLevel == t.AiSound, $"{t.Name} explosion AI sound level");
    if (t.Flash is { } f) Require(e.ImageSpaceModifier.FormKey == f.Imod && e.ISRadius > 0, $"{t.Name} flash");
}

var cloud = check.Hazards.Single(h => h.FormKey.ID == 0x855);
Require(cloud.Spell.FormKey.ID == 0x856 && cloud.Lifetime >= 10f, "smoke cloud lingers and applies its spell");
var invis = check.MagicEffects.Single(x => x.FormKey.ID == 0x857);
Require(invis.Archetype is IMagicEffectArchetypeGetter ia && ia.Type == MagicEffectArchetype.TypeEnum.Invisibility, "smoke gives invisibility");
var fear = check.MagicEffects.Single(x => x.FormKey.ID == 0x864);
Require(fear.Archetype is IMagicEffectArchetypeGetter fa && fa.Type == MagicEffectArchetype.TypeEnum.Demoralize, "thunder causes fear");

Console.WriteLine($"Wrote and verified {outPath} ({new FileInfo(outPath).Length} bytes): " +
                  $"{types.Length} pot types, masters {string.Join(", ", masters)}");

record PotType(string Tag, string Name, uint BaseId, uint ItemId, uint EffectId, uint Value, string Description,
    FormKey? Keyword, (FormKey Effect, float Magnitude, int Duration)? Burst, FormKey? Sound2, FormKey? ImpactSet,
    FormKey? Placed, (FormKey Imod, float Radius)? Flash, SoundLevel AiSound);
