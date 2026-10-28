// Builds FirePots.esp (ESL-flagged) with Mutagen, then reads it back and checks every record.
//
// Two pot types, each with the same four records:
//   FP_<Type>Pot            MISC  the item you carry, loot and craft
//   FP_<Type>PotProjectile  PROJ  what flies: a missile with gravity that explodes on any contact
//   FP_<Type>PotExplosion   EXPL  the shatter: sounds, decal, damage burst (and for fire, a vanilla
//                                 burning patch left on the ground)
//   FP_<Type>PotBurstEnch   ENCH  the burst's elemental damage (vanilla enchantment effect)
//
// No recipes: pots are made anywhere with FirePots.dll's pot-making menu (craft key, H), which
// reads its container and filling lists from FirePots.ini.
//
// Meshes: the plugin leaves item, projectile and explosion models empty; FirePots.dll copies them
// from vanilla forms at load (see [Visuals] in FirePots.ini).
//
// Usage: MakePlugin <output folder>

using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;

const string PluginName = "FirePots.esp";

var skyrim = ModKey.FromFileName("Skyrim.esm");
var dawnguard = ModKey.FromFileName("Dawnguard.esm");
var dragonborn = ModKey.FromFileName("Dragonborn.esm");
FormKey Sk(uint id) => new FormKey(skyrim, id);

// Vanilla forms, IDs from Mutagen.Bethesda.FormKeys.SkyrimSE.
var L_PROJECTILE = Sk(0x088767);

// --- Pot types. Fixed form IDs: FirePots.dll looks these up, so they must never move.
var types = new[]
{
    new PotType("Fire", "Fire Pot", 0x800, 15, BurstDamage: 30f,
        DamageEffect: Sk(0x04605A),      // EnchFireDamageFFContact
        DamageKeyword: Sk(0x01CEAD),     // MagicDamageFire
        Sound2: Sk(0x042526),            // TRPOilIgniteLowSD
        ImpactSet: Sk(0x01C2AF),         // MAGFirebolt01ImpactSet (scorch)
        Placed: Sk(0x03E074)),           // FireSmLongHazard (burning patch)
    new PotType("Frost", "Frost Pot", 0x810, 20, BurstDamage: 30f,
        DamageEffect: Sk(0x04605B),      // EnchFrostDamageFFContact (also drains stamina)
        DamageKeyword: Sk(0x01CEAE),     // MagicDamageFrost
        Sound2: Sk(0x023992),            // FXExplosionIceSD
        ImpactSet: Sk(0x018A2E),         // MAGFrost01ImpactSet (frost decal)
        Placed: null),
};
var TRPOilLampBreak = Sk(0x08BBC6);  // shatter sound, both types
const float BlastRadius = 160f;      // units (~2.3 m); the arc preview draws this ring

var outDir = args.Length > 0 ? args[0] : ".";
Directory.CreateDirectory(outDir);
var outPath = Path.Combine(outDir, PluginName);

var modKey = ModKey.FromFileName(PluginName);
var mod = new SkyrimMod(modKey, SkyrimRelease.SkyrimSE);
mod.ModHeader.Author = "akeaneschmidt1998";
mod.ModHeader.Description = "Fire Pots: throwable fire and frost pots with wind-up and arc preview (requires FirePots.dll / SKSE).";
mod.IsSmallMaster = true;  // ESL flag: takes no load order slot

foreach (var t in types)
{
    var burst = new ObjectEffect(new FormKey(modKey, t.BaseId + 3), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}PotBurstEnch",
        Name = $"{t.Name} Burst",
        Flags = ObjectEffect.Flag.NoAutoCalc,
        CastType = CastType.FireAndForget,
        TargetType = TargetType.Touch,
        EnchantType = ObjectEffect.EnchantTypeEnum.Enchantment,
    };
    var effect = new Effect { Data = new EffectData { Magnitude = t.BurstDamage, Area = 0, Duration = 0 } };
    effect.BaseEffect.SetTo(t.DamageEffect);
    burst.Effects.Add(effect);
    mod.ObjectEffects.Add(burst);

    var explosion = new Explosion(new FormKey(modKey, t.BaseId + 2), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}PotExplosion",
        Name = t.Name,
        Force = 30f,
        Damage = 5f,
        Radius = BlastRadius,
        ISRadius = 0f,
        VerticalOffsetMult = 0f,
        Flags = 0,
        SoundLevel = SoundLevel.Normal,
    };
    explosion.ObjectEffect.SetTo(burst.FormKey);
    explosion.Sound1.SetTo(TRPOilLampBreak);
    explosion.Sound2.SetTo(t.Sound2);
    explosion.ImpactDataSet.SetTo(t.ImpactSet);
    if (t.Placed is { } placed)
    {
        explosion.PlacedObject.SetTo(placed);
    }
    mod.Explosions.Add(explosion);

    // Missile + Gravity: ballistic arc, explodes on the first thing it touches.
    // Speed is overwritten by FirePots.dll on every throw (wind-up); Gravity 1 = normal gravity.
    var projectile = new Projectile(new FormKey(modKey, t.BaseId + 1), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}PotProjectile",
        Name = t.Name,
        Type = Projectile.TypeEnum.Missile,
        Flags = Projectile.Flag.Explosion,
        Gravity = 1.0f,
        Speed = 1200f,
        Range = 30000f,
        CollisionRadius = 8f,
        Lifetime = 10f,
        ImpactForce = 0f,
        SoundLevel = (uint)SoundLevel.Normal,
    };
    projectile.Explosion.SetTo(explosion.FormKey);
    projectile.CollisionLayer.SetTo(L_PROJECTILE);
    mod.Projectiles.Add(projectile);

    var pot = new MiscItem(new FormKey(modKey, t.BaseId), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}Pot",
        Name = t.Name,
        Value = t.Value,
        Weight = 1.0f,
        Keywords = new() { t.DamageKeyword.ToLink<IKeywordGetter>() },
    };
    mod.MiscItems.Add(pot);
}

// Only Skyrim.esm is referenced now; the load order just keeps masters ordered if that changes.
mod.BeginWrite
    .ToPath(outPath)
    .WithLoadOrder(skyrim, ModKey.FromFileName("Update.esm"), dawnguard, ModKey.FromFileName("HearthFires.esm"), dragonborn)
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
Require(!check.ConstructibleObjects.Any(), "no recipes (crafting is the DLL's menu)");

foreach (var t in types)
{
    var pot = check.MiscItems.Single(m => m.FormKey.ID == t.BaseId);
    Require(pot.Name?.String == t.Name, $"{t.Name} item");

    var p = check.Projectiles.Single(x => x.FormKey.ID == t.BaseId + 1);
    Require(p.Type == Projectile.TypeEnum.Missile && p.Flags.HasFlag(Projectile.Flag.Explosion) && p.Gravity > 0.5f, $"{t.Name} projectile is an exploding missile with gravity");
    Require(p.Explosion.FormKey.ID == t.BaseId + 2, $"{t.Name} projectile -> explosion");
    Require(p.CollisionLayer.FormKey == L_PROJECTILE, $"{t.Name} projectile collision layer");

    var e = check.Explosions.Single(x => x.FormKey.ID == t.BaseId + 2);
    Require(e.ObjectEffect.FormKey.ID == t.BaseId + 3, $"{t.Name} explosion -> burst");
    Require(t.Placed is null ? e.PlacedObject.IsNull : e.PlacedObject.FormKey == t.Placed, $"{t.Name} explosion placed object");
    Require(Math.Abs(e.Radius - BlastRadius) < 0.01f, $"{t.Name} explosion radius");

    var en = check.ObjectEffects.Single(x => x.FormKey.ID == t.BaseId + 3);
    Require(en.Effects.Count == 1 && en.Effects[0].BaseEffect.FormKey == t.DamageEffect, $"{t.Name} burst effect");
}

Console.WriteLine($"Wrote and verified {outPath} ({new FileInfo(outPath).Length} bytes): " +
                  $"{types.Length} pot types, masters {string.Join(", ", masters)}");

record PotType(string Tag, string Name, uint BaseId, uint Value, float BurstDamage, FormKey DamageEffect,
    FormKey DamageKeyword, FormKey Sound2, FormKey ImpactSet, FormKey? Placed);
